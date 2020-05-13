import numpy as np
import pandas as pd
import librosa
import json
from joblib import Parallel, delayed
from numba import jit


TARGET_RATE = 44100

OBSERVATION_INTERVAL_SECONDS = 0.5 # in seconds
OBSERVATION_INTERVAL_LEN = int(OBSERVATION_INTERVAL_SECONDS * TARGET_RATE)

ENERGY_INTERVAL_SECONDS = 0.05
ENERGY_INTERVAL_LEN = int(ENERGY_INTERVAL_SECONDS * TARGET_RATE)

OVERLAP = 0.5
OVERLAP_LEN = int(ENERGY_INTERVAL_LEN * (1 - OVERLAP))

FEATURES_INTERVAL_DELTA = int(ENERGY_INTERVAL_LEN * 0.5)


@jit(nopython=True)
def numba_average_amplitude(interval):
    if len(interval) == 0:
        return 0
    if len(interval) == 1:
        return interval[0]
    return np.abs(interval[1:] - interval[:-1]).sum() / len(interval)


def numba_extract_subfingerprint(subarray):
    """
    partial fingerprint of audio-part
    Return:
        32-digit number
    """
    M = 32
    idxs = np.logspace(0, np.log(len(subarray)), M + 1, base=np.e, endpoint=True, dtype=np.int16)
    # make indices reasonable or it is [1,1,1,1,2,2,2,3,3,4,...]
    t = 1
    for i in range(2, len(idxs)):
        if idxs[i] - idxs[i - 1] <= t:
            idxs[i] = idxs[i - 1] + t
            if i == 6:
                t = 2
            elif i == 12:
                t = 3
    
    cumulative_energies = 0.0
    subfingerprint = 1

    cumulative_energies += numba_average_amplitude(subarray[:idxs[1]])
    for m in range(1, M):
        prev_mean_energy = cumulative_energies / m
        cumulative_energies += numba_average_amplitude(subarray[idxs[m]:idxs[m+1]])
        current_mean_energy = cumulative_energies / (m + 1)
        subfingerprint <<= 1
        if current_mean_energy >= prev_mean_energy:
            subfingerprint += 1
    return subfingerprint


def numba_extract_fingerprint(np_audio):
    """
    audio fingerprint
    Return:
        np.array((N))
    """
    total_fingerprint = []
    current_start = 0
    while current_start < len(np_audio) - 1:
        current_end = min(current_start + OBSERVATION_INTERVAL_LEN, 
                          len(np_audio) - 1)
        current_observation_interval = np_audio[current_start : current_end]
        
        final_start = 0 # ОТНОСИТЕЛЬНО current_observation_interval
        energy_max = 0
    
        for energy_interval_start in range(0, 
                                           OBSERVATION_INTERVAL_LEN - ENERGY_INTERVAL_LEN, 
                                           OVERLAP_LEN):
    
            energy_interval = current_observation_interval[energy_interval_start : 
                                                           min(energy_interval_start + ENERGY_INTERVAL_LEN,
                                                               len(current_observation_interval) - 1)]
            current_energy = numba_average_amplitude(energy_interval)
            if current_energy > energy_max:
                final_start = energy_interval_start
                energy_max = current_energy
        
        final_end = final_start + ENERGY_INTERVAL_LEN # ОТНОСИТЕЛЬНО current_observation_interval
        
        features_start = max(0, current_start + final_start - FEATURES_INTERVAL_DELTA) # ОТНОСИТЕЛЬНО np_audio
        features_end = min(current_start + final_end + FEATURES_INTERVAL_DELTA,
                           len(np_audio) - 1) # ОТНОСИТЕЛЬНО np_audio
        total_fingerprint.append(numba_extract_subfingerprint(np_audio[features_start: features_end]))
         
        current_start += final_end
    return np.array(total_fingerprint)


MAESTRO_DIR = "/data/maestro-v2.0.0/"
CSV_PATH = MAESTRO_DIR + "maestro-v2.0.0.csv"
OUT_DIR = "/data/fingerprints/"
REDIS_PORT = 6379
    
def process_audioID(audioID):
        filename = metadata.iloc[audioID].audio_filename
    
        np_audio, sr = librosa.load(MAESTRO_DIR + filename, sr=TARGET_RATE, mono=True)
        fprints = numba_extract_fingerprint(np_audio)
        np.save(OUT_DIR + str(audioID) + ".npy", fprints)
    
        print(audioID)
   

def processs_all_audios():    
    metadata = pd.read_csv(CSV_PATH)

    Parallel(n_jobs=8)(delayed(process_audioID)(audioID) for audioID in range(metadata.shape[0]))


def upload_to_redis():
    metadata = pd.read_csv(CSV_PATH)
    r = redis.Redis(host='localhost', port=REDIS_PORT, db=0)

    total_dict = dict() # {fp : {id : percentage}}
    for i in range(metadata.shape[0]):
        a = np.load(OUT_DIR + str(i) + ".npy")
        a_dict = dict() # {fp : count}
        # count statistics
        for fp in a:
            if fp not in a_dict.keys():
                a_dict[fp] = 1
            else:
                a_dict[fp] += 1
        # fill total_dict
        for fp in a_dict.keys():
            if fp not in total_dict.keys():
                total_dict[fp] = dict()
            total_dict[fp][i] = a_dict[fp] / a.shape[0]
    # fill redis
    for fp in total_dict.keys():
        r.hset(str(fp), mapping=total_dict[fp])