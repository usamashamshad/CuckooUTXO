#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <iomanip>
#include <random>
#include <unordered_map>
#include <ctime>
#include <chrono>

using namespace std;

// UTXO Data Structure
struct UTXOValue {
    bool coinbase;
    uint64_t height;
    uint64_t amount;
    string script;
    string address;
    UTXOValue() : coinbase(false), height(0), amount(0) {}
    UTXOValue(bool cb, uint64_t h, uint64_t amt, const string& scr, const string& addr)
        : coinbase(cb), height(h), amount(amt), script(scr), address(addr) {}
};

// Perfect Cuckoo Filter Implementation (Your UTXOManager)
class UTXOManager {
private:
    struct BucketEntry {
        uint32_t fingerprint;
        bool selector_bit;
        UTXOValue value;
        BucketEntry() : fingerprint(0), selector_bit(false) {}
        BucketEntry(uint32_t fp, bool sb, const UTXOValue& val)
            : fingerprint(fp), selector_bit(sb), value(val) {}
    };
    static const size_t BUCKET_SIZE = 4;
    static const size_t NUM_BUCKETS = 1 << 19;
    static const size_t MAX_RELOCATIONS = 500;
    static const uint32_t UNIVERSE_BITS = 32;
    static const uint32_t FINGERPRINT_BITS = 13;
    vector<vector<BucketEntry>> buckets;

    uint32_t crc32_hash(const string& key) const {
        const uint32_t polynomial = 0xEDB88320;
        uint32_t crc = 0xFFFFFFFF;
        for (char c : key) {
            crc ^= static_cast<uint8_t>(c);
            for (int i = 0; i < 8; i++) {
                crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
            }
        }
        return ~crc;
    }

    void get_bucket_fingerprint(const string& key, uint32_t& bucket, uint32_t& fingerprint) const {
        uint32_t h = crc32_hash(key);
        bucket = h & ((1 << 19) - 1);
        fingerprint = (h >> 19) & ((1 << FINGERPRINT_BITS) - 1);
    }

    uint32_t get_alt_bucket(uint32_t bucket, uint32_t fingerprint) const {
        uint32_t fp_hash = fingerprint * 0xCC9E2D51;
        return bucket ^ (fp_hash & ((1 << 19) - 1));
    }

    bool relocate(uint32_t bucket, uint32_t fingerprint, const UTXOValue& value, size_t depth) {
        if (depth > MAX_RELOCATIONS) return false;
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        if (buckets[alt_bucket].size() < BUCKET_SIZE) {
            buckets[alt_bucket].push_back({fingerprint, true, value});
            return true;
        }
        size_t evict_idx = rand() % buckets[alt_bucket].size();
        BucketEntry evicted = buckets[alt_bucket][evict_idx];
        buckets[alt_bucket][evict_idx] = {fingerprint, true, value};
        uint32_t new_bucket = get_alt_bucket(alt_bucket, evicted.fingerprint);
        if (evicted.selector_bit) {
            new_bucket = alt_bucket ^ (evicted.fingerprint * 0xCC9E2D51 & ((1 << 19) - 1));
        }
        if (buckets[new_bucket].size() < BUCKET_SIZE) {
            buckets[new_bucket].push_back(evicted);
            return true;
        }
        return relocate(new_bucket, evicted.fingerprint, evicted.value, depth + 1);
    }

public:
    UTXOManager() : buckets(NUM_BUCKETS) { srand(time(nullptr)); }

    bool add_utxo(const string& key, const UTXOValue& value) {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);
        for (const auto& entry : buckets[bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == false) return false;
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        for (const auto& entry : buckets[alt_bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == true) return false;
        }
        if (buckets[bucket].size() < BUCKET_SIZE) {
            buckets[bucket].push_back({fingerprint, false, value});
            return true;
        }
        if (buckets[alt_bucket].size() < BUCKET_SIZE) {
            buckets[alt_bucket].push_back({fingerprint, true, value});
            return true;
        }
        return relocate(bucket, fingerprint, value, 0);
    }

    bool delete_utxo(const string& key) {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);
        for (auto it = buckets[bucket].begin(); it != buckets[bucket].end(); ++it) {
            if (it->fingerprint == fingerprint && it->selector_bit == false) {
                buckets[bucket].erase(it);
                return true;
            }
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        for (auto it = buckets[alt_bucket].begin(); it != buckets[alt_bucket].end(); ++it) {
            if (it->fingerprint == fingerprint && it->selector_bit == true) {
                buckets[alt_bucket].erase(it);
                return true;
            }
        }
        return false;
    }

    const UTXOValue* get_utxo(const string& key) const {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);
        for (const auto& entry : buckets[bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == false) {
                return &entry.value;
            }
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        for (const auto& entry : buckets[alt_bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == true) {
                return &entry.value;
            }
        }
        return nullptr;
    }

    size_t count() const {
        size_t total = 0;
        for (const auto& bucket : buckets) total += bucket.size();
        return total;
    }
};

// CSV Parsing Functions
vector<string> split_utxo_line(const string& line, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream iss(line);
    bool in_quotes = false;
    char current_quote = '\0';
    for (char c : line) {
        if ((c == '\'' || c == '"') && !in_quotes) {
            in_quotes = true;
            current_quote = c;
            token += c;
        } else if (c == current_quote && in_quotes) {
            in_quotes = false;
            token += c;
        } else if (c == delimiter && !in_quotes) {
            tokens.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

vector<string> try_split_utxo_line(const string& line) {
    for (char delim : {'\t', ',', ';'}) {
        auto tokens = split_utxo_line(line, delim);
        if (tokens.size() >= 6) return tokens;
    }
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

UTXOValue parse_utxo_data(const vector<string>& tokens) {
    UTXOValue utxo;
    try {
        if (tokens.size() < 6) throw runtime_error("Not enough columns");
        utxo.coinbase = tokens[1] == "1";
        utxo.height = stoull(tokens[2]);
        utxo.amount = stoull(tokens[3]);
        utxo.script = tokens[4];
        utxo.address = tokens[5];
    } catch (const exception& e) {
        cerr << "Error parsing UTXO data: " << e.what() << endl;
    }
    return utxo;
}

// Load UTXOs from CSV and Test FPR
void test_fpr(UTXOManager& cuckoo, const string& filename,
              ofstream& out) {
    ifstream file(filename);
    if (!file) {
        cerr << "Error: Cannot open file " << filename << endl;
        return;
    }

    string line;
    size_t line_num = 0, total_utxos = 0;
    vector<string> keys;
    UTXOManager temp_cuckoo;
    string current_date = "01/01";

    if (getline(file, line)) {
        if (line.find("txid:index") != string::npos || line.find("coinbase") != string::npos) {
            cout << "Skipping header row\n";
        }
    }

    mt19937 rng(time(nullptr));
    auto generate_random_key = [&rng]() {
        string chars = "0123456789abcdef";
        string key(64, '0');
        uniform_int_distribution<int> dist(0, 15);
        for (char& c : key) c = chars[dist(rng)];
        key += ":0";
        return key;
    };

    vector<double> cuckoo_insert_times, cuckoo_delete_times, cuckoo_query_times;

    while (getline(file, line)) {
        line_num++;
        if (line.empty()) continue;

        vector<string> tokens = try_split_utxo_line(line);
        if (tokens.size() < 6) continue;

        UTXOValue value = parse_utxo_data(tokens);
        string key = tokens[0];
        keys.push_back(key);

        // Measure insert time for Cuckoo
        auto start = chrono::high_resolution_clock::now();
        bool cuckoo_success = temp_cuckoo.add_utxo(key, value);
        auto end = chrono::high_resolution_clock::now();
        cuckoo_insert_times.push_back(chrono::duration<double, nano>(end - start).count());

        total_utxos++;

        // Simulate deletion of some UTXOs for timing
        if (total_utxos % 1000 == 0 && !keys.empty()) {
            string delete_key = keys[rand() % keys.size()];
            start = chrono::high_resolution_clock::now();
            temp_cuckoo.delete_utxo(delete_key);
            end = chrono::high_resolution_clock::now();
            cuckoo_delete_times.push_back(chrono::duration<double, nano>(end - start).count());
        }

        if (total_utxos % 100000 == 0 || total_utxos == 1600000) {
            int day = (total_utxos / 100000) + 1;
            if (day > 26) day = 26;
            current_date = string("01/") + to_string(day);

            size_t cuckoo_fp = 0;
            size_t num_queries = 10000;
            for (size_t i = 0; i < num_queries; ++i) {
                string query_key = generate_random_key();
                while (find(keys.begin(), keys.end(), query_key) != keys.end()) {
                    query_key = generate_random_key();
                }
                start = chrono::high_resolution_clock::now();
                if (temp_cuckoo.get_utxo(query_key)) cuckoo_fp++;
                end = chrono::high_resolution_clock::now();
                cuckoo_query_times.push_back(chrono::duration<double, nano>(end - start).count());
            }

            double cuckoo_fpr = 100.0 * cuckoo_fp / num_queries;

            double avg_cuckoo_insert = cuckoo_insert_times.empty() ? 0 : accumulate(cuckoo_insert_times.begin(), cuckoo_insert_times.end(), 0.0) / cuckoo_insert_times.size();
            double avg_cuckoo_delete = cuckoo_delete_times.empty() ? 0 : accumulate(cuckoo_delete_times.begin(), cuckoo_delete_times.end(), 0.0) / cuckoo_delete_times.size();
            double avg_cuckoo_query = cuckoo_query_times.empty() ? 0 : accumulate(cuckoo_query_times.begin(), cuckoo_query_times.end(), 0.0) / cuckoo_query_times.size();

            out << current_date << "," << total_utxos << "," << cuckoo_fpr << ","
                << avg_cuckoo_insert << "," << avg_cuckoo_delete << "," << avg_cuckoo_query << "\n";
            cout << "Date: " << current_date << " | UTXOs: " << total_utxos
                 << " | Cuckoo FPR: " << cuckoo_fpr << "%"
                 << " | Cuckoo Insert: " << avg_cuckoo_insert << "ns | Cuckoo Delete: " << avg_cuckoo_delete << "ns"
                 << " | Cuckoo Query: " << avg_cuckoo_query << "ns\n";

            temp_cuckoo = UTXOManager();
            keys.clear();
            cuckoo_insert_times.clear();
            cuckoo_delete_times.clear();
            cuckoo_query_times.clear();
        }
    }

    file.close();
}

int main() {
    UTXOManager cuckoo;
    ofstream out("fpr_results.csv");
    out << "Date,Num_Transactions,Cuckoo_FPR,Cuckoo_Insert_ns,Cuckoo_Delete_ns,Cuckoo_Query_ns\n";

    cout << "Loading UTXO dataset from combined_utxos.csv...\n";
    test_fpr(cuckoo, "combined_utxos.csv", out);

    out.close();
    cout << "Results written to fpr_results.csv\n";
    return 0;
}