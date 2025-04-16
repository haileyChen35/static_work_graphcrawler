#include <iostream>
#include <string>
#include <queue>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <stdexcept>
#include "rapidjson/error/error.h"
#include "rapidjson/reader.h"
#include <thread>


struct ParseException : std::runtime_error, rapidjson::ParseResult {
    ParseException(rapidjson::ParseErrorCode code, const char* msg, size_t offset) : 
        std::runtime_error(msg), 
        rapidjson::ParseResult(code, offset) {}
};


#include <rapidjson/document.h>
#include <chrono>

using namespace std;
using namespace rapidjson;

bool debug = false;
const int MAX_THREADS = 8;
std::mutex mtx;

// Updated service URL
const string SERVICE_URL = "http://hollywood-graph-crawler.bridgesuncc.org/neighbors/";

// Function to HTTP ecnode parts of URLs. for instance, replace spaces with '%20' for URLs
string url_encode(CURL* curl, string input) {
  char* out = curl_easy_escape(curl, input.c_str(), input.size());
  string s = out;
  curl_free(out);
  return s;
}

// Callback function for writing response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// Function to fetch neighbors using libcurl with debugging
string fetch_neighbors(CURL* curl, const string& node) {

    string url = SERVICE_URL + url_encode(curl, node);
    string response;

    if (debug)
      cout << "Sending request to: " << url << endl;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L); // Verbose Logging

    // Set a User-Agent header to avoid potential blocking by the server
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++-Client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        cerr << "CURL error: " << curl_easy_strerror(res) << endl;
    } else {
      if (debug)
        cout << "CURL request successful!" << endl;
    }

    // Cleanup
    curl_slist_free_all(headers);

    if (debug) 
      cout << "Response received: " << response << endl;  // Debug log

    return (res == CURLE_OK) ? response : "{}";
}

// Function to parse JSON and extract neighbors
vector<string> get_neighbors(const string& json_str) {
    vector<string> neighbors;
    try {
      Document doc;
      doc.Parse(json_str.c_str());
      
      if (doc.HasMember("neighbors") && doc["neighbors"].IsArray()) {
        for (const auto& neighbor : doc["neighbors"].GetArray())
	  neighbors.push_back(neighbor.GetString());
      }
    } catch (const ParseException& e) {
      std::cerr<<"Error while parsing JSON: "<<json_str<<std::endl;
      throw e;
    }
    return neighbors;
}


void process_nodes(CURL* curl, const vector<string>& nodes_to_process,
                  unordered_set<string>& visited,
                  vector<string>& next_level) {
    
    curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL in thread" << endl;
        return;
    }
    for (const auto& s : nodes_to_process) {
        try {
            if (debug) cout << "Trying to expand " << s << "\n";
            auto neighbors = get_neighbors(fetch_neighbors(curl, s));
            for (const auto& neighbor : neighbors) {
                if (debug) cout << "neighbor " << neighbor << "\n";
                
                lock_guard<mutex> lock(mtx);
                if (!visited.count(neighbor)) {
                    visited.insert(neighbor);
                    next_level.push_back(neighbor);
                }
            }
        } catch (const exception& e) {
            cerr << "Error while fetching neighbors of: " << s << endl;
            cerr << "Error: " << e.what() << endl;
        }
    }
    curl_easy_cleanup(curl);

}




// Parallel BFS Traversal Function
vector<vector<string>> parallel_bfs(const string& start, int depth) {
    vector<vector<string>> levels;
    unordered_set<string> visited;
    
    levels.push_back({start});
    visited.insert(start);

    CURL* curl = curl_easy_init();
    if (!curl) {
        cerr << "Failed to initialize CURL" << endl;
        return {};
    }

    for (int d = 0; d < depth; d++) {
        if (debug)
            cout << "Starting level: " << d << " with " << levels[d].size() << " nodes" << endl;
        
        levels.push_back({});
        vector<string>& current_level = levels[d];
        vector<string>& next_level = levels[d+1];
        
        int num_nodes = current_level.size();
        int num_threads = min(MAX_THREADS, num_nodes);
        
        if (debug)
            cout << "Using " << num_threads << " threads for this level" << endl;
        
        vector<thread> threads;
        vector<vector<string>> node_chunks(num_threads);
        
        for (int i = 0; i < num_nodes; i++) {
            node_chunks[i % num_threads].push_back(current_level[i]);
        }
        
        vector<CURL*> thread_curls(num_threads, nullptr);

        // Launch threads
        for (int i = 0; i < num_threads; i++) {
            threads.emplace_back(
                process_nodes,
                &thread_curls[i], 
                ref(node_chunks[i]),
                ref(visited),
                ref(next_level)
            );
        }
        
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
    
    curl_easy_cleanup(curl);
    return levels;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <node_name> <depth>\n";
        return 1;
    }

    string start_node = argv[1];
    int depth;
    try {
        depth = stoi(argv[2]);
    } catch (const exception& e) {
        cerr << "Error: Depth must be an integer.\n";
        return 1;
    }

    const auto start = chrono::steady_clock::now();
    
    auto result = parallel_bfs(start_node, depth);
    
    for (const auto& level : result) {
        for (const auto& node : level) {
            cout << "- " << node << "\n";
        }
        cout << level.size() << " nodes at this level\n";
    }
    
    const auto finish = chrono::steady_clock::now();
    const chrono::duration<double> elapsed = finish - start;
    cout << "Time to crawl: " << elapsed.count() << "s\n";
    
    return 0;
}
