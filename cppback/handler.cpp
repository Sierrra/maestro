#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/JSON/Array.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/URI.h>

#include "acl_cpp/lib_acl.hpp"

extern "C" {
#include <sndfile.h>
}

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <map>
#include <cstdlib>
#include <utility>
#include <unordered_set>

using namespace Poco::Net;
using namespace Poco::Util;
using namespace std;


// audio (as in py)
const int TARGET_RATE = 44100;
const double OBSERVATION_INTERVAL_SECONDS = 0.5; // in seconds
const size_t OBSERVATION_INTERVAL_LEN = 22050;
const double ENERGY_INTERVAL_SECONDS = 0.05;
const size_t ENERGY_INTERVAL_LEN = 2205;
const double OVERLAP = 0.5;
const size_t OVERLAP_LEN = 1102;
const size_t FEATURES_INTERVAL_DELTA = 1102;
// fingerprint (as in py)
const uint64_t DEFAULT_SUBFINGERPRINT = 4294967295;
const int SUBFINGERPRINT_SIZE = 32;
// redis
const char* REDIS_PATH = "redis:6379";
// neighbours
const int DEFAULT_N_NEIGHBOURS = 3;
// http connection
const bool KEEP_ALIVE = false;
const int MAX_QUEUED = 100;
const int MAX_THREADS = 6;
const int SERVER_SOCKET = 9090;


std::vector<double> linspace(double start, double stop, int num, bool endpoint) {
    int div = num;
    if (endpoint) {
        div = num - 1;
    }
    double delta = stop - start;
    double step = delta / div;
    std::vector<double> y = std::vector<double>(num, 0);
    for (int i = 0; i < num; i++) {
        y[i] = start + i * delta / div;
    }
    if (endpoint && num > 1) {
        y[y.size() - 1] = stop;
    }
    return y;
}

std::vector<double> logspace(double start, double stop, int num, bool endpoint, double base = 10.0) {
    auto y = linspace(start, stop, num, endpoint);
    for (auto& elem : y) {
        elem = std::pow(base, elem);
    }
    return y;
}

class Interval {
private:
    const std::vector<double> &data;
    size_t start, stop;
public:
    Interval(const std::vector<double> &d, size_t b, size_t f): data(d), start(b), stop(std::min(f, data.size() - 1)) {
    }
    size_t len() const {
        if (stop <= start) {
            return 0;
        }
        return stop - start;
    }
    double at(int i) const {
        if (start + i >= data.size()) {
            throw "out of interval";
        }
        return data[start + i];
    }
    const std::vector<double>& getData() const {
        return data;
    }
    size_t getStart() const {
        return start;
    }
    size_t getStop() const {
        return stop;
    }
};

std::vector<int> generateIndices(int M, size_t len) {
    auto ttt = logspace(0, std::log2(static_cast<double>(len)), M + 1, true, 2);
    std::vector<int> idxs = std::vector<int>(M + 1, 0);
    int t = 1;
    for (size_t i = 0; i < idxs.size(); ++i) {
        idxs[i] = std::floor(ttt[i]);
        if (i < 2) {
            continue;
        }
        if (idxs[i] - idxs[i - 1] <= t) {
            idxs[i] = idxs[i - 1] + t;
            if (i == 6) {
                t = 2;
            } else if (i == 12) {
                t = 3;
            }
        }
    }
    return idxs;
}

double averageAmplitude(const Interval &interval) {
    if (interval.len() == 0) {
        return 0;
    }
    if (interval.len() == 1) {
        return interval.at(0);
    }
    double res = 0;
    for (size_t i = 1; i < interval.len(); ++i) {
        res += std::abs(interval.at(i) - interval.at(i - 1));
    }
    return res / static_cast<double>(interval.len());
}

uint64_t extractSubfingerprint(const Interval &array) {
    if (array.len() == 0) {
        return DEFAULT_SUBFINGERPRINT;
    }
    int M = SUBFINGERPRINT_SIZE;
    auto idxs = generateIndices(M, array.len());

    double cumulative_energies = 0;
    uint64_t subfingerprint = 1;

    cumulative_energies += averageAmplitude(Interval(array.getData(), 
                                                     array.getStart(), 
                                                     array.getStart() + idxs[1]));
    for (int m = 1; m < M; ++m) {
        double prev_mean_energy = cumulative_energies / m;
        cumulative_energies += averageAmplitude(Interval(array.getData(), 
                                                         array.getStart() + idxs[m],
                                                         array.getStart() + idxs[m+1]));
        double current_mean_energy = cumulative_energies / (m + 1);
        subfingerprint <<= 1;
        if (current_mean_energy >= prev_mean_energy) {
            subfingerprint += 1;
        }
    }
    return subfingerprint;
}


void extractFingerprint(const std::vector<double> &np_audio, std::vector<uint64_t>* fingerprint) {
    if (np_audio.size() == 0) {
        return; // so that np_audio.size() - 1 don't become grand
    }
    size_t current_start = 0;
    
    while (current_start < np_audio.size() - 1) {
        size_t current_end = std::min(current_start + OBSERVATION_INTERVAL_LEN, 
                                      np_audio.size() - 1);
        Interval current_observation_interval({np_audio,
                                              current_start,
                                              current_end});
        
        size_t final_start = 0;
        double energy_max = 0;
    
        for (size_t energy_interval_start = 0;
             energy_interval_start < OBSERVATION_INTERVAL_LEN - ENERGY_INTERVAL_LEN;
             energy_interval_start += OVERLAP_LEN) {
    
            Interval energy_interval({current_observation_interval.getData(),
                                      current_observation_interval.getStart() + energy_interval_start,
                                      current_observation_interval.getStart() + energy_interval_start + ENERGY_INTERVAL_LEN});
            double current_energy = averageAmplitude(energy_interval);
            if (current_energy > energy_max) {
                final_start = energy_interval_start;
                energy_max = current_energy;
            }
        }
        size_t final_end = final_start + ENERGY_INTERVAL_LEN;
        
        size_t features_start = current_start + final_start - FEATURES_INTERVAL_DELTA;
        if (current_start + final_start < FEATURES_INTERVAL_DELTA) {
            features_start = 0;
        }
        size_t features_end = std::min(current_start + final_end + FEATURES_INTERVAL_DELTA,
                                       np_audio.size() - 1);

        fingerprint->emplace_back(extractSubfingerprint(Interval(np_audio,
                                                                 features_start,
                                                                 features_end)));
        
        current_start += final_end;
    }
}

void readWavToVector(const std::string& path, std::vector<double>* np_audio) {
    SF_INFO sfinfo = {0, 0, 0, 0, 0, 0};
    SNDFILE *sndfile = sf_open(path.data(), SFM_READ, &sfinfo);
    
    double dst[sfinfo.channels];
    
    for (;;) {
        if (sf_read_double(sndfile, dst, sfinfo.channels) < sfinfo.channels) {
            break;
        }

        double elem = 0;
        for (int i = 0; i < sfinfo.channels; ++i) {
            elem += dst[i];
        }
        np_audio->emplace_back(elem / sfinfo.channels);
    }
    if (sf_close(sndfile) != 0) {
        // error occured
    }
}


bool sortByVal(const pair<int, int> &a, const pair<int, int> &b) { 
    return (a.second > b.second);
} 

void getNNeighbours(const std::vector<uint64_t>& keys, int n, std::vector<int>* result) {
    int conn_timeout = 10000, rw_timeout = 10000, max_conns=10000;

    std::map<uint64_t, int> orig_count;
    for (const auto& elem: keys) {
        if (orig_count.find(elem) == orig_count.end()) {
            orig_count[elem] = 1;
        } else {
            orig_count[elem] += 1;
        }
    }

    std::map<int, int> found;
    std::unordered_set<int> seen_keys;

    for (const auto& uint_key : keys) {
        // bind redis_list command with redis connection
        acl::redis_client conn(REDIS_PATH, conn_timeout, rw_timeout);
        acl::redis_hash cmd_hash(&conn);
        std::map<acl::string, acl::string> acl_result;
        // convert uint64_t to char*
        stringstream ss;
        ss << uint_key;
        const char* key = ss.str().c_str();
        // get response
        if (cmd_hash.hgetall(key, acl_result) == false) {
            // no such key
        } else {
            for (auto& elem : acl_result) {
                int melody_id = std::atoi(elem.first.c_str());
                char * end;
                double percentage = std::strtof(elem.second.c_str(), &end);
                if (found.find(melody_id) == found.end()) {
                    if (seen_keys.find(uint_key) != seen_keys.end()) {
                        found[melody_id] = -static_cast<int>(orig_count[uint_key] * percentage);
                    } else {
                        found[melody_id] = 0;
                    }
                }
                if (seen_keys.find(uint_key) == seen_keys.end()) {
                    found[melody_id] += 1 - static_cast<int>(orig_count[uint_key] * percentage);
                } else {
                    found[melody_id]++;
                }
            }
        }
        seen_keys.emplace(uint_key);
    }

    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(found.size());
    for (auto it : found) {
        pairs.emplace_back(std::make_pair(it.first, std::abs(it.second)));
    }
    found.clear();

    std::sort(pairs.begin(), pairs.end(), sortByVal);

    for (size_t i = 0; i < n && i < pairs.size(); ++i) {
        result->emplace_back(pairs[i].first);
    }
}


class MyRequestHandler : public HTTPRequestHandler
{
public:
    virtual void handleRequest(HTTPServerRequest &req, HTTPServerResponse &resp) {
        // get n neighbors
        int n_neighbours = DEFAULT_N_NEIGHBOURS;
        // get path from request
        std::string path;
        
        Poco::URI uri(req.getURI());
        for (const auto& pair: uri.getQueryParameters()) {
            if (pair.first == std::string("n")) {
                n_neighbours = std::atoi(pair.second.c_str());
            } else if (pair.first == std::string("path")) {
                path = pair.second;
            }
        }
        if (path.size() == 0) {
            resp.setStatus(HTTPResponse::HTTP_BAD_REQUEST);
                
            std::string ss("No path to file provided");
            
            Poco::JSON::Array json_array;
            json_array.add(Poco::Dynamic::Var(ss));
                
            resp.setContentType("application/json");
            json_array.stringify(resp.send());
            resp.send().flush();
            return;
        }
        // process audio
        std::vector<double> audio;
        audio.reserve(1000);
        readWavToVector(path, &audio);
        // extract fingerprint
        std::vector<uint64_t> fingerprint;
        fingerprint.reserve(1000);
        extractFingerprint(audio, &fingerprint);
        audio.clear();
        // get neighbours
        std::vector<int> result;
        result.reserve(n_neighbours);
        getNNeighbours(fingerprint, n_neighbours, &result);
        fingerprint.clear();
        // write response
        Poco::JSON::Array json_array;
        for (auto& elem : result) {
            json_array.add(Poco::Dynamic::Var(elem));
        }
        
        resp.setStatus(HTTPResponse::HTTP_OK);
        resp.setContentType("application/json");

        std::ostream & outflow = resp.send();
        json_array.stringify(outflow);
        outflow.flush();
    }
};

class MyRequestHandlerFactory : public HTTPRequestHandlerFactory
{
public:
    virtual HTTPRequestHandler* createRequestHandler(const HTTPServerRequest &)
    {
        return new MyRequestHandler;
    }
};

class MyServerApp : public ServerApplication
{
protected:
    int main(const vector<string> &)
    {
        HTTPServerParams* pParams = new HTTPServerParams;
        pParams->setKeepAlive(KEEP_ALIVE);
        pParams->setMaxQueued(MAX_QUEUED);
        pParams->setMaxThreads(MAX_THREADS);
        HTTPServer s(new MyRequestHandlerFactory, ServerSocket(SERVER_SOCKET), pParams);

        s.start();
        cout << endl << "Server started" << endl;

        waitForTerminationRequest();  // wait for CTRL-C or kill

        cout << endl << "Shutting down..." << endl;
        s.stop();

        return Application::EXIT_OK;
    }
};

int main(int argc, char** argv)
{
    MyServerApp app;
    return app.run(argc, argv);
}
