package main

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"github.com/go-redis/redis/v7"
	"github.com/jackc/pgx/v4"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"path"
	"strconv"
	"strings"
//	"io"
)
type Melody struct {
	Index int64
	Composer string
	Title string
	AudioFilename string
	Duration float32
}
var  client = redis.NewClient(&redis.Options{
Addr:     os.Getenv("REDIS_DATABASE_URL"), 
DB:       0,  // use default DB
})

var cpp_route = os.Getenv("CPP_ROUTE")
var dir_path = os.Getenv("UPLOADED_DIR_PATH")
var audio_dir_path = os.Getenv("AUDIO_DIR_PATH")

func audio(w http.ResponseWriter, req *http.Request) {
	queryValues := req.URL.Query()
	raw_id := queryValues.Get("id")
	id, err := strconv.ParseInt(raw_id, 10, 64); 
	if err != nil {
		http.Error(w, "Problem with id.", 404)
		return
	}
	audio_metainfo := get_metainfo(id)
	Openfile, err := os.Open(path.Join(audio_dir_path,audio_metainfo.AudioFilename))
	
	if err != nil {
		//File not found, send 404
		http.Error(w, "File not found.", 404)
		return
	}
	defer Openfile.Close()
	FileHeader := make([]byte, 512)
	Openfile.Read(FileHeader)
	FileContentType := http.DetectContentType(FileHeader)
	FileStat, _ := Openfile.Stat()                     //Get info from file
	FileSize := strconv.FormatInt(FileStat.Size(), 10) //Get file size as a string
	filename := strings.Split(audio_metainfo.AudioFilename, "/")[1]
	w.Header().Add("Content-Disposition", "attachment; filename=\""+filename+"\"")
	w.Header().Set("Content-Type", FileContentType)
	w.Header().Set("Content-Length", FileSize)
	w.Header().Set("Content-Transfer-Encoding", "binary")
	Openfile.Seek(0, 0)
	http.ServeFile(w, req, path.Join(audio_dir_path,audio_metainfo.AudioFilename))
	//io.Copy(w, Openfile) 
}

func get_similar(w http.ResponseWriter, req *http.Request) {
	queryValues := req.URL.Query()
	n := queryValues.Get("n")
	req.ParseMultipartForm(10 << 23)
	file, handler, err := req.FormFile("file")
	if err != nil {
		fmt.Println("Error Retrieving the File")
		fmt.Println(err)
		return
	}
	defer file.Close()
	fmt.Printf("Uploaded File: %+v\n", handler.Filename)
	fmt.Printf("File Size: %+v\n", handler.Size)
	fmt.Printf("MIME Header: %+v\n", handler.Header)
	response := []Melody {}
	if err != nil {
		fmt.Println(err)
	}
	fileBytes, err := ioutil.ReadAll(file)

	if err != nil {
		fmt.Println(err)
	}
	log.Print("file readed")
	md5_bytes := md5.Sum(fileBytes)
	md5_hash := hex.EncodeToString(md5_bytes[:])
	fmt.Println("MD5 formed")
	count , err := client.LLen(md5_hash).Result()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	} else if (count == 0) {
		file_err := ioutil.WriteFile(path.Join(dir_path, md5_hash + ".wav"), fileBytes,0644)
		if file_err != nil {
			log.Fatal(file_err)
		}
		fmt.Println("File written")
		http_client := &http.Client{}
		req, err := http.NewRequest("GET", cpp_route, nil)
		if err != nil {
			log.Fatal(err)
			log.Fatal("Problem with request creation")
		}
		q := req.URL.Query()
		q.Add("n", n)
		q.Add("path", path.Join(dir_path, md5_hash + ".wav"))
		fmt.Println(q)
		req.URL.RawQuery = q.Encode()
		fmt.Println(req.URL)
		cpp_raw_result, err := http_client.Do(req)
		if err != nil {
			log.Fatal(err)
			log.Fatal("Problem with request to cpp")
		}
		fmt.Println(cpp_raw_result)
		raw_result, _ := ioutil.ReadAll(cpp_raw_result.Body)
		var candidate_list []int64
		json.Unmarshal(raw_result, &candidate_list)
		for _, id := range candidate_list {
			response = append(response, get_metainfo(id))
			_, err := client.LPush(md5_hash, id).Result()
			if err != nil {
				log.Print(err)
			}
			
		}
	} else {
		count_ := int(count)
		for i:=0 ; i < count_ ; i++ {
			i_int64 := int64(i)
			id , err := client.LIndex(md5_hash, int64(count_) - i_int64 - 1).Int64()
			if err != nil {
				log.Fatal("problem")
			}
			response = append(response, get_metainfo(id))
		}
	}
	log.Print(response)
	js, err := json.Marshal(response)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Write(js)
}

func get_metainfo(id int64) Melody {
	conn, err := pgx.Connect(context.Background(), os.Getenv("POSTGRESQL_DATABASE_URL"))
	if err != nil {
		fmt.Fprintf(os.Stderr, "Unable to connect to database: %v\n", err)
		os.Exit(1)
	}
	var index int64
	var canonical_composer string
	var canonical_title string
	var audio_filename string
	var duration float32
	defer conn.Close(context.Background())
	err = conn.QueryRow(context.Background(), 
	"select index, canonical_composer, canonical_title, audio_filename, duration from public.music_metadata where index=$1", id).Scan(
		&index, &canonical_composer, &canonical_title, &audio_filename, &duration)
	return Melody{index, canonical_composer, canonical_title, audio_filename, duration}
}


func main() {

	http.HandleFunc("/get_similar", get_similar)
	http.HandleFunc("/audio", audio)
	http.ListenAndServe(":8090", nil)
}