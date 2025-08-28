#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <iomanip>

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

// Perfect Cuckoo Filter Implementation
class UTXOManager {
private:
    struct BucketEntry {
        uint32_t fingerprint; // 13-bit fingerprint
        bool selector_bit;    // 0 for primary, 1 for alternate bucket
        UTXOValue value;     // Stored value
        BucketEntry() : fingerprint(0), selector_bit(false) {}
        BucketEntry(uint32_t fp, bool sb, const UTXOValue& val)
            : fingerprint(fp), selector_bit(sb), value(val) {}
    };

    static const size_t BUCKET_SIZE = 4;
    static const size_t NUM_BUCKETS = 1 << 19; // 524,288 buckets
    static const size_t MAX_RELOCATIONS = 500; // Increased for robustness
    static const uint32_t UNIVERSE_BITS = 32;  // 32-bit universe
    static const uint32_t FINGERPRINT_BITS = 13; // 32 - 19 = 13 bits
    vector<vector<BucketEntry>> buckets;

    // CRC32 (bijective hash function for 32-bit universe)
    uint32_t crc32_hash(const string& key) const {
        const uint32_t polynomial = 0xEDB88320; // Standard CRC32 polynomial
        uint32_t crc = 0xFFFFFFFF;

        for (char c : key) {
            crc ^= static_cast<uint8_t>(c);
            for (int i = 0; i < 8; i++) {
                crc = (crc >> 1) ^ ((crc & 1) ? polynomial : 0);
            }
        }
        return ~crc; // Finalize CRC
    }

    // Compute bucket and fingerprint
    void get_bucket_fingerprint(const string& key, uint32_t& bucket, uint32_t& fingerprint) const {
        uint32_t h = crc32_hash(key);
        bucket = h & ((1 << 19) - 1); // Lower 19 bits for bucket
        fingerprint = (h >> 19) & ((1 << FINGERPRINT_BITS) - 1); // Upper 13 bits
    }

    // Compute alternate bucket
    uint32_t get_alt_bucket(uint32_t bucket, uint32_t fingerprint) const {
        uint32_t fp_hash = fingerprint * 0xCC9E2D51;
        return bucket ^ (fp_hash & ((1 << 19) - 1));
    }

    // Relocation logic for insertion
    bool relocate(uint32_t bucket, uint32_t fingerprint, const UTXOValue& value, size_t depth) {
        if (depth > MAX_RELOCATIONS) {
            return false;
        }

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
    UTXOManager() : buckets(NUM_BUCKETS) {
        srand(time(nullptr));
    }

    bool add_utxo(const string& key, const UTXOValue& value) {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);

        // Check for duplicates using fingerprint and selector bit
        for (const auto& entry : buckets[bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == false) {
                cerr << "UTXO with fingerprint " << fingerprint << " already exists in primary bucket\n";
                return false;
            }
        }
        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        for (const auto& entry : buckets[alt_bucket]) {
            if (entry.fingerprint == fingerprint && entry.selector_bit == true) {
                cerr << "UTXO with fingerprint " << fingerprint << " already exists in alternate bucket\n";
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

        if (relocate(bucket, fingerprint, value, 0)) {
            return true;
        }

        cerr << "Failed to insert UTXO with fingerprint " << fingerprint << " - relocation failed\n";
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

    bool remove_utxo(const string& key) {
        uint32_t bucket, fingerprint;
        get_bucket_fingerprint(key, bucket, fingerprint);

        auto& primary = buckets[bucket];
        for (auto it = primary.begin(); it != primary.end(); ++it) {
            if (it->fingerprint == fingerprint && !it->selector_bit) {
                primary.erase(it);
                return true;
            }
        }

        uint32_t alt_bucket = get_alt_bucket(bucket, fingerprint);
        auto& alternate = buckets[alt_bucket];
        for (auto it = alternate.begin(); it != alternate.end(); ++it) {
            if (it->fingerprint == fingerprint && it->selector_bit) {
                alternate.erase(it);
                return true;
            }
        }

        cerr << "UTXO with fingerprint " << fingerprint << " not found\n";
        return false;
    }

    size_t count() const {
        size_t total = 0;
        for (const auto& bucket : buckets) {
            total += bucket.size();
        }
        return total;
    }

    void display_stats() const {
        size_t primary = 0, secondary = 0, empty = 0;
        for (const auto& bucket : buckets) {
            if (bucket.empty()) empty++;
            for (const auto& entry : bucket) {
                entry.selector_bit ? secondary++ : primary++;
            }
        }
        cout << "\n=== UTXO Manager Statistics ===\n"
             << "Total UTXOs: " << count() << "\n"
             << "Primary bucket entries: " << primary << "\n"
             << "Secondary bucket entries: " << secondary << "\n"
             << "Empty buckets: " << empty << " ("
             << fixed << setprecision(1) << (100.0 * empty / NUM_BUCKETS) << "%)\n"
             << "Load factor: " << (100.0 * (primary + secondary) / (NUM_BUCKETS * BUCKET_SIZE)) << "%\n";
    }
};

// Enhanced CSV Parser
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
    if (!token.empty()) {
        tokens.push_back(token);
    }
    return tokens;
}

vector<string> try_split_utxo_line(const string& line) {
    for (char delim : {'\t', ',', ';'}) {
        auto tokens = split_utxo_line(line, delim);
        if (tokens.size() >= 6) {
            return tokens;
        }
    }
    vector<string> tokens;
    istringstream iss(line);
    string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
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
        cerr << "Tokens received: ";
        for (const auto& t : tokens) cerr << "[" << t << "] ";
        cerr << endl;
    }
    return utxo;
}

void load_utxo_dataset(UTXOManager& manager, const string& filename) {
    ifstream file(filename);
    if (!file) {
        cerr << "Error: Cannot open file " << filename << endl;
        return;
    }
    string line;
    size_t line_num = 0, loaded = 0, skipped = 0;
    if (getline(file, line)) {
        if (line.find("txid:index") != string::npos || line.find("coinbase") != string::npos) {
            cout << "Skipping header row\n";
        } else {
            if (line_num < 5) {
                cout << "\nRaw line " << line_num + 1 << ": [" << line << "]\n";
            }
            vector<string> tokens = try_split_utxo_line(line);
            if (line_num < 5) {
                cout << "Line " << line_num + 1 << " parsed as " << tokens.size() << " columns:\n";
                for (size_t i = 0; i < tokens.size(); i++) {
                    cout << "  Column " << i << ": [" << tokens[i] << "]\n";
                }
            }
            if (tokens.size() >= 6) {
                UTXOValue value = parse_utxo_data(tokens);
                if (manager.add_utxo(tokens[0], value)) loaded++;
                else skipped++;
            } else {
                cerr << "Line " << line_num + 1 << ": Only " << tokens.size() << " columns found\n";
                skipped++;
            }
        }
        line_num++;
    }
    while (getline(file, line)) {
        line_num++;
        if (line.empty()) {
            skipped++;
            continue;
        }
        if (line_num < 5 || (line_num >= 15096 && line_num <= 15125)) {
            cout << "\nRaw line " << line_num << ": [" << line << "]\n";
        }
        vector<string> tokens = try_split_utxo_line(line);
        if (line_num < 5 || (line_num >= 15096 && line_num <= 15125)) {
            cout << "Line " << line_num << " parsed as " << tokens.size() << " columns:\n";
            for (size_t i = 0; i < tokens.size(); i++) {
                cout << "  Column " << i << ": [" << tokens[i] << "]\n";
            }
        }
        if (tokens.size() < 6) {
            cerr << "Line " << line_num << ": Only " << tokens.size() << " columns found\n";
            skipped++;
            continue;
        }
        try {
            UTXOValue value = parse_utxo_data(tokens);
            if (manager.add_utxo(tokens[0], value)) loaded++;
            else skipped++;
        } catch (const exception& e) {
            cerr << "Line " << line_num << ": Error - " << e.what() << "\n";
            skipped++;
        }
    }
    cout << "\n=== Loading Results ===\n"
         << "Total lines processed: " << line_num << "\n"
         << "Successfully loaded:   " << loaded << "\n"
         << "Skipped:               " << skipped << "\n";
}

// Interactive Interface
void print_utxo_details(const string& key, const UTXOValue* utxo) {
    if (!utxo) {
        cout << "UTXO with key " << key << " not found\n";
        return;
    }
    cout << "\n=== UTXO Details ===\n"
         << "Key:      " << key << "\n"
         << "Coinbase: " << (utxo->coinbase ? "Yes" : "No") << "\n"
         << "Height:   " << utxo->height << "\n"
         << "Amount:   " << utxo->amount << " satoshis\n"
         << "Script:   " << utxo->script << "\n"
         << "Address:  " << utxo->address << "\n\n";
}

void show_menu() {
    cout << "\n=== Bitcoin UTXO Manager ===\n"
         << "1. Lookup UTXO\n"
         << "2. Add UTXO\n"
         << "3. Remove UTXO\n"
         << "4. Show Statistics\n"
         << "5. Exit\n"
         << "Enter choice: ";
}

void run_interactive(UTXOManager& manager) {
    string input;
    while (true) {
        show_menu();
        getline(cin, input);
        if (input == "5") break;
        string key;
        if (input != "4") {
            cout << "Enter txid:index: ";
            getline(cin, key);
        }
        switch (input[0]) {
            case '1': {
                const UTXOValue* utxo = manager.get_utxo(key);
                print_utxo_details(key, utxo);
                break;
            }
            case '2': {
                UTXOValue new_utxo;
                cout << "Coinbase (0/1): ";
                getline(cin, input);
                new_utxo.coinbase = input == "1";
                cout << "Block height: ";
                getline(cin, input);
                new_utxo.height = stoull(input);
                cout << "Amount (satoshis): ";
                getline(cin, input);
                new_utxo.amount = stoull(input);
                cout << "Script: ";
                getline(cin, new_utxo.script);
                cout << "Address: ";
                getline(cin, new_utxo.address);
                if (manager.add_utxo(key, new_utxo)) {
                    cout << "UTXO added successfully!\n";
                }
                break;
            }
            case '3': {
                if (manager.remove_utxo(key)) {
                    cout << "UTXO removed successfully!\n";
                }
                break;
            }
            case '4': {
                manager.display_stats();
                break;
            }
            default:
                cout << "Invalid choice\n";
        }
    }
}

int main() {
    UTXOManager manager;
    const string filename = "combined_utxos.csv";

    cout << "Loading UTXO dataset from " << filename << "...\n";
    load_utxo_dataset(manager, filename);

    if (manager.count() == 0) {
        cout << "\nCRITICAL: No UTXOs loaded. Please verify:\n"
             << "1. File exists and is readable\n"
             << "2. The format matches expected (6 columns)\n"
             << "3. Check the raw lines and parsing output above\n"
             << "4. Try viewing the file with: cat -A " << filename << " | head -n 5\n";
    } else {
        run_interactive(manager);
    }

    cout << "\nProgram exiting. Final UTXO count: " << manager.count() << endl;
    return 0;
}