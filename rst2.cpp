#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <iomanip>
#include <unordered_map>
#include <random>
#include <chrono>
#include <cmath>
#include <unordered_set>

using namespace std;

// UTXO Data Structure (simplified for testing)
struct UTXOValue {
    bool coinbase;
    uint64_t height;
    uint64_t amount;
    UTXOValue() : coinbase(false), height(0), amount(0) {}
    UTXOValue(bool cb, uint64_t h, uint64_t amt) : coinbase(cb), height(h), amount(amt) {}
};

// Perfect Cuckoo Filter
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

    size_t BUCKET_SIZE;
    size_t NUM_BUCKETS;
    size_t MAX_RELOCATIONS;
    uint32_t FINGERPRINT_BITS;
    uint32_t BUCKET_BITS;
    vector<vector<BucketEntry>> buckets;
    mt19937_64 rng;

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
        bucket = h & ((1 << BUCKET_BITS) - 1);
        fingerprint = (h >> BUCKET_BITS) & ((1 << FINGERPRINT_BITS) - 1);
    }

    uint32_t get_alt_bucket(uint32_t bucket, uint32_t fingerprint) const {
        uint32_t fp_hash = fingerprint * 0xCC9E2D51;
        return bucket ^ (fp_hash & ((1 << BUCKET_BITS) - 1));
    }

    bool relocate(uint32_t bucket, uint32_t fingerprint, const UTXOValue& value, size_t depth) {
        if (depth > MAX_RELOCATIONS) {
            return false;
        }
        if (bucket >= NUM_BUCKETS) {
            cerr << "Invalid bucket index: " << bucket << endl;
            return false;
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        if (alt_bucket >= NUM_BUCKETS) {
            cerr << "Invalid alt_bucket index: " << alt_bucket << endl;
            return false;
        }
        if (buckets[alt_bucket].size() < BUCKET_SIZE) {
            buckets[alt_bucket].push_back({fingerprint, true, value});
            return true;
        }
        size_t evict_idx = uniform_int_distribution<size_t>(0, buckets[alt_bucket].size() - 1)(rng);
        BucketEntry evicted = buckets[alt_bucket][evict_idx];
        buckets[alt_bucket][evict_idx] = {fingerprint, true, value};
        uint32_t new_bucket = get_alt_bucket(alt_bucket, evicted.fingerprint);
        if (evicted.selector_bit) {
            new_bucket = alt_bucket ^ (evicted.fingerprint * 0xCC9E2D51 & ((1 << BUCKET_BITS) - 1));
        }
        if (new_bucket >= NUM_BUCKETS) {
            cerr << "Invalid new_bucket index: " << new_bucket << endl;
            return false;
        }
        if (buckets[new_bucket].size() < BUCKET_SIZE) {
            buckets[new_bucket].push_back(evicted);
            return true;
        }
        return relocate(new_bucket, evicted.fingerprint, evicted.value, depth + 1);
    }

public:
    UTXOManager(size_t num_buckets, size_t bucket_size, uint32_t fingerprint_bits)
        : BUCKET_SIZE(bucket_size), NUM_BUCKETS(num_buckets), MAX_RELOCATIONS(100),
          FINGERPRINT_BITS(fingerprint_bits), BUCKET_BITS(ceil(log2(num_buckets))),
          buckets(num_buckets), rng(chrono::steady_clock::now().time_since_epoch().count()) {
        for (auto& bucket : buckets) {
            bucket.reserve(bucket_size);
        }
    }

    bool add_utxo(const string& key, const UTXOValue& value) {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);
        if (bucket >= NUM_BUCKETS) {
            cerr << "Invalid bucket index: " << bucket << endl;
            return false;
        }
        for (const auto& entry : buckets[bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == false) {
                return false;
            }
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        if (alt_bucket >= NUM_BUCKETS) {
            cerr << "Invalid alt_bucket index: " << alt_bucket << endl;
            return false;
        }
        for (const auto& entry : buckets[alt_bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == true) {
                return false;
            }
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

    const UTXOValue* get_utxo(const string& key) const {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);
        if (bucket >= NUM_BUCKETS) {
            cerr << "Invalid bucket index: " << bucket << endl;
            return nullptr;
        }
        for (const auto& entry : buckets[bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == false) {
                return &entry.value;
            }
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        if (alt_bucket >= NUM_BUCKETS) {
            cerr << "Invalid alt_bucket index: " << alt_bucket << endl;
            return nullptr;
        }
        for (const auto& entry : buckets[alt_bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == true) {
                return &entry.value;
            }
        }
        return nullptr;
    }

    size_t count() const {
        size_t total = 0;
        for (const auto& bucket : buckets) {
            total += bucket.size();
        }
        return total;
    }

    double get_load_factor() const {
        return static_cast<double>(count()) / (NUM_BUCKETS * BUCKET_SIZE);
    }
};

// Bitcoin Core-like Mempool
class BitcoinCoreMempool {
private:
    unordered_map<string, UTXOValue> utxo_map;

public:
    bool add_utxo(const string& key, const UTXOValue& value) {
        if (utxo_map.find(key) != utxo_map.end()) {
            return false;
        }
        utxo_map[key] = value;
        return true;
    }

    const UTXOValue* get_utxo(const string& key) const {
        auto it = utxo_map.find(key);
        if (it != utxo_map.end()) {
            return &it->second;
        }
        return nullptr;
    }

    size_t count() const {
        return utxo_map.size();
    }
};

// Generate random txid:index key
string generate_random_key(mt19937_64& rng) {
    stringstream ss;
    ss << hex << setfill('0') << setw(16) << rng();
    ss << ":" << (rng() % 1000);
    return ss.str();
}

// Measure FPR
double measure_fpr(const UTXOManager& manager, const unordered_set<string>& existing_keys, size_t num_queries, mt19937_64& rng) {
    size_t false_positives = 0;
    for (size_t i = 0; i < num_queries; ++i) {
        string key;
        do {
            key = generate_random_key(rng);
        } while (existing_keys.find(key) != existing_keys.end());
        if (manager.get_utxo(key) != nullptr) {
            false_positives++;
        }
    }
    return static_cast<double>(false_positives) / num_queries;
}

int main() {
    ofstream csv_file("fpr_results.csv");
    csv_file << "Filter_Size,Fingerprint_Bits,UTXO_Count,PCF_FPR,Core_FPR\n";

    mt19937_64 rng(chrono::steady_clock::now().time_since_epoch().count());
    vector<size_t> utxo_counts = {100000, 500000, 1000000, 2000000, 5000000};
    vector<pair<size_t, uint32_t>> filter_configs = {
        {1 << 18, 13}, // 262,144 buckets, 13-bit fingerprint
        {1 << 19, 13}, // 524,288 buckets, 13-bit fingerprint
        {1 << 20, 15}, // 1,048,576 buckets, 15-bit fingerprint
        {1 << 20, 17}  // 1,048,576 buckets, 17-bit fingerprint (Carbyne-like)
    };

    for (const auto& config : filter_configs) {
        size_t num_buckets = config.first;
        uint32_t fingerprint_bits = config.second;
        UTXOManager pcf_manager(num_buckets, 4, fingerprint_bits);
        BitcoinCoreMempool core_manager;

        for (size_t count : utxo_counts) {
            cout << "Testing with " << count << " UTXOs, " << num_buckets << " buckets, " << fingerprint_bits << " fingerprint bits...\n";

            unordered_set<string> inserted_keys;
            inserted_keys.reserve(count);
            vector<string> keys;
            keys.reserve(count);
            // Pre-generate unique keys
            while (keys.size() < count) {
                string key = generate_random_key(rng);
                if (inserted_keys.insert(key).second) {
                    keys.push_back(key);
                }
            }

            size_t inserted = 0;
            for (const auto& key : keys) {
                if (pcf_manager.get_load_factor() >= 0.90) {
                    break;
                }
                UTXOValue value(true, rng() % 1000000, rng() % 100000000);
                if (pcf_manager.add_utxo(key, value) && core_manager.add_utxo(key, value)) {
                    inserted++;
                }
                if (inserted % 10000 == 0) {
                    cout << "Inserted " << inserted << " UTXOs, Load Factor: " << pcf_manager.get_load_factor() * 100 << "%\n";
                }
            }

            size_t num_queries = 1000000;
            double pcf_fpr = measure_fpr(pcf_manager, inserted_keys, num_queries, rng);
            double core_fpr = 0.0;

            csv_file << num_buckets << "," << fingerprint_bits << "," << inserted << "," << pcf_fpr << "," << core_fpr << "\n";
            cout << "Inserted: " << inserted << ", PCF FPR: " << pcf_fpr * 100 << "%, Core FPR: " << core_fpr * 100 << "%\n";
        }
    }

    csv_file.close();
    cout << "Results written to fpr_results.csv\n";
    return 0;
}