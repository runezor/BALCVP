#ifndef VALUE_PREDICTOR_HH
#define VALUE_PREDICTOR_HH

#include <cstdint>
#include <unordered_map>
#include <bitset>
#include <vector>
#include <optional>
#include <stdexcept>
#include <cassert>
#include <deque>
#include <iostream>
#include <functional>

using PC = uint64_t;        // Program Counter type
using Value = uint64_t;     // Value type
using InstSeqNum = uint64_t; // Instruction sequence number type

constexpr size_t MAX_HIST = 200;
constexpr size_t MAX_BRANCH_SPEC_DISTANCE = 64;

enum Confidence { low=0, medium=1, high=2 };

class LastCommittedValueTable {
public:
    bool hasValue(PC pc) const {
        auto it = table.find(pc);
        return (it != table.end());
    }

    Value lookup(PC pc) const {
        auto it = table.find(pc);
        return (it != table.end()) ? it->second : 0;
    }
    void update(PC pc, Value val){
        table[pc] = val;
    }

private:
    std::unordered_map<PC, Value> table;
};


class EqualityPredictorEntry {
public:
    EqualityPredictorEntry(uint64_t tag) : tag(tag), taken_counter(0), not_taken_counter(0) { }
    EqualityPredictorEntry() : tag(0), taken_counter(0), not_taken_counter(0) {}

    void update(bool outcome) {
        if (outcome){
            if (taken_counter<7){
                taken_counter++;
            } else {
                if (not_taken_counter>0){
                    not_taken_counter--;
                }
            }
        } else {
            if (not_taken_counter<7){
                not_taken_counter++;
            } else {
                if (taken_counter>0){
                    taken_counter--;
                }
            }
        }
    }

    bool getDirection(){
        return taken_counter > not_taken_counter;
    }

    void decay(){
        if (taken_counter>not_taken_counter) taken_counter--;
        if (not_taken_counter>taken_counter) not_taken_counter--;
    }

    Confidence getConfidence(){
        bool medium = (taken_counter==(2*not_taken_counter+1)) or (not_taken_counter==(2*taken_counter+1));
        bool low = (taken_counter<(2*not_taken_counter+1)) && (not_taken_counter<(2*taken_counter+1));

        size_t conf = 2-(medium+(low<<1));

        if (conf==0){
            return Confidence::low;
        } else if (conf==1){
            return Confidence::medium;
        } else {
            return Confidence::high;
        }
    }

    uint64_t tag;
    size_t taken_counter;
    size_t not_taken_counter;
};

class PathTracker {
public:
    PathTracker(size_t ghist_bits, size_t index_size, size_t tag_size)
        : ghist_bits(ghist_bits)
        , index_size(index_size)
        , tag_size(tag_size)
        , folded_path(0)
        , outcome_buffer()
    {
        if (index_size + tag_size > 31) {
            throw std::invalid_argument("index_size + tag_size must be <= 31");
        }
    }

    void addBranch(bool outcome) {
        if (ghist_bits==0)
            return;
        // Get the msb of the outcome buffer (ghist_bits)
        bool old_outcome = outcome_buffer[ghist_bits - 1];
        // Shift outcome buffer left and set new outcome
        outcome_buffer <<= 1;
        outcome_buffer[0] = outcome;
        // Do a circular left shift of the folded_path
        unsigned int msb = (folded_path >> (index_size + tag_size - 1)) & 1;
        folded_path = ((folded_path << 1) & ((1u << (index_size + tag_size)) - 1)) | msb;
        // Figure out where the old outcome is
        size_t fold_position = ghist_bits % (index_size + tag_size);
        folded_path ^= (old_outcome << fold_position);
        // Add in the most recent outcome
        folded_path ^= outcome;
    }
    void revertBranches(size_t num) {
        if (ghist_bits==0)
            return;
        for (size_t i = 0; i < num; i++) {
            // Get the most recent outcome
            bool outcome = outcome_buffer[0];
            outcome_buffer >>= 1;
            // Remove it from the folded_path
            folded_path ^= outcome;

            // Add in old result
            size_t fold_position = ghist_bits % (index_size + tag_size);
            bool old_outcome = outcome_buffer[ghist_bits-1];
            folded_path ^= (old_outcome << fold_position);

            // Circular right shift the folded_path
            unsigned int lsb = folded_path & 1;
            folded_path >>= 1;
            folded_path |= (lsb << (index_size + tag_size - 1));
        }
    }
    unsigned getIndex(PC pc) const {
        unsigned combined = (pc ^ (pc >> 2) ^ (pc >> 5)) ^ folded_path;

        return combined & ((1u << index_size) - 1);
    }
    unsigned getTag(PC pc) const {
        unsigned combined = (pc ^ (pc >> 2) ^ (pc >> 5)) ^ folded_path;
        return (combined >> index_size) & ((1u << tag_size) - 1);
    }

    size_t ghist_bits;
    size_t index_size;
    size_t tag_size;
    unsigned int folded_path;
    std::bitset<MAX_HIST> outcome_buffer;   
};

class EqualityPredictorComponent {
public:
    EqualityPredictorComponent(size_t size, size_t ghist_bits, 
                              size_t index_bits, size_t tag_bits)
        : path(ghist_bits, index_bits, tag_bits)
        , components(size)
    {}

    EqualityPredictorEntry& getEntryConflict(PC pc) {
        unsigned index = path.getIndex(pc);

        assert(index<components.size());

        return components[index];
    }

    std::optional<std::reference_wrapper<EqualityPredictorEntry>> getEntry(PC pc) {
        unsigned tag = path.getTag(pc);
        EqualityPredictorEntry& entry = getEntryConflict(pc);
        
        if (entry.tag == tag) {
            return std::ref(entry);
        }
        
        return std::nullopt;
    }

    void allocate(PC pc, bool outcome) {
        unsigned index = path.getIndex(pc);
        unsigned tag = path.getTag(pc);

        assert(index<components.size());

        components[index] = EqualityPredictorEntry(tag);
        components[index].update(outcome);
    }

    void onCommit(PC pc, bool outcome) {
        unsigned index = path.getIndex(pc);
        unsigned tag = path.getTag(pc);

        assert(index<components.size());

        if (components[index].tag == tag) {
            components[index].update(outcome);
        }
    }

    void addBranch(bool outcome) {
        path.addBranch(outcome);
    }
    void revertBranches(size_t num) {
        path.revertBranches(num);
    }
private:
    PathTracker path;
    std::vector<EqualityPredictorEntry> components;
};

struct ComponentConfig {
    size_t size;
    size_t ghist_bits;
    size_t index_bits;
    size_t tag_bits;
};

class EqualityPredictor {
public:
    EqualityPredictor(const std::vector<ComponentConfig>& configs) {
        components.reserve(configs.size());
        for (const auto& config : configs) {
            components.emplace_back(config.size, config.ghist_bits, 
                                    config.index_bits, config.tag_bits);
        }
    }

    void updateOnBranch(InstSeqNum seqNum, bool outcome) {
        if (branch_queue.size() >= MAX_BRANCH_SPEC_DISTANCE) {
            throw std::runtime_error("Exceeded maximum speculative branch distance");
        }

        branch_queue.push_back(seqNum);

        for (auto& component : components) {
            component.addBranch(outcome);
        }
    }

    struct PredictionData {
        std::optional<std::reference_wrapper<EqualityPredictorEntry>> primary;
        size_t primary_index;
        std::optional<std::reference_wrapper<EqualityPredictorEntry>> alt;
        size_t alt_index;
    };

    PredictionData getPredictingEntries(PC pc) {
        PredictionData result{
            std::nullopt,
            0,
            std::nullopt,
            0
        };
        
        for (size_t i = 0; i < components.size(); i++) {
            auto& component = components[i];
            auto entryOpt = component.getEntry(pc);

            if (entryOpt.has_value()) {
                auto& entry = entryOpt.value().get();

                if (!result.primary.has_value() || 
                    (entry.getConfidence() >= result.primary.value().get().getConfidence())) {
                    result.alt = result.primary;
                    result.alt_index = result.primary_index;
                    
                    result.primary = entryOpt.value();
                    result.primary_index = i;
                }
            }
        }

        return result;
    }

    std::pair<Confidence, bool> predict(PC pc) {
        PredictionData pd = getPredictingEntries(pc);

        if (pd.primary.has_value()) {
            auto& entry = pd.primary.value().get();
            return {entry.getConfidence(), entry.getDirection()};
        }

        return {Confidence::low, false};
    }

    std::optional<std::reference_wrapper<EqualityPredictorEntry>> predictingEntry(PC pc) {
        PredictionData pd = getPredictingEntries(pc);
        return pd.primary;
    }

    void onValueCommit(PC pc, bool wasEqual, bool debug = false){
        PredictionData pd = getPredictingEntries(pc);

        bool prediction = pd.primary.has_value() && pd.primary.value().get().getDirection();
        size_t longest_hitting_index = 0;

        for(size_t i=0; i<components.size(); i++){
            auto entryOpt = components[i].getEntry(pc);

            if (entryOpt.has_value()) {
                bool is_alt = (pd.alt.has_value() && i==pd.alt_index);
                bool is_primary = (i==pd.primary_index);
                bool is_longer_than_primary = (i>pd.primary_index);

                longest_hitting_index = i;

                auto& entry = entryOpt.value().get();

                if (is_longer_than_primary) {
                    entry.update(wasEqual);
                } else if (is_primary) {
                    assert(i==0 || (pd.alt.has_value()));
                    if (i==0 || entry.getConfidence()!=Confidence::high
                             || (pd.alt.has_value() && pd.alt.value().get().getConfidence() < high)
                             || (pd.alt.has_value() && pd.alt.value().get().getDirection() != wasEqual) ){
                        entry.update(wasEqual);
                    } else if (i>0 && entry.getConfidence()==Confidence::high && (pd.alt.value().get().getConfidence()==Confidence::high && pd.alt.value().get().getDirection() == wasEqual)){
                        entry.decay();
                    }
                } else if (is_alt) {
                    if (pd.primary.value().get().getConfidence()!=high){
                        entry.update(wasEqual);
                    }
                }
            }
        }

        // Allocation
        if (prediction!=wasEqual){
            size_t s = longest_hitting_index + (1); // todo add small rand
            
            size_t r = components.size();

            for (size_t i = s; i < components.size(); i++) {
                auto& entry = components[i].getEntryConflict(pc);
                if (entry.getConfidence() != high) {
                    r = i;
                    components[i].allocate(pc, wasEqual);
                    break;
                }
            }

            for (size_t i = s; i < r; i++) {
                auto& entry = components[i].getEntryConflict(pc);

                assert(entry.getConfidence() == high);

                if (rand() % 4 == 0)
                    entry.decay();
            }
        }
    }

    void onBranchCommit(InstSeqNum seqNum){
        assert(branch_queue.front() == seqNum);
        branch_queue.pop_front();
    }
    void squash(InstSeqNum seqNum){
        size_t num_to_revert = 0;
        
        // Count how many branches we need to revert
        while (!branch_queue.empty() && branch_queue.back() >= seqNum) {
            num_to_revert++;
            branch_queue.pop_back();
        }

        for (auto& component : components) {
            component.revertBranches(num_to_revert);
        }
    }

private:
    std::vector<EqualityPredictorComponent> components;
    std::deque<InstSeqNum> branch_queue;
};

struct ValuePredictorParams {
    // Add configuration parameters here
};

class ValuePredictor {
public:
    ValuePredictor(const ValuePredictorParams& params) : params(params), ep({
        // Base component with small history
        {.size = 1024, .ghist_bits = 8, .index_bits = 10, .tag_bits = 0},
        // Medium component
        {.size = 4096, .ghist_bits = 16, .index_bits = 12, .tag_bits = 12},
        // Large component with longer history
        {.size = 8192, .ghist_bits = 32, .index_bits = 13, .tag_bits = 13}
    }) {}

    ~ValuePredictor() = default;

    // Deleted copy/move operations to prevent accidental copies
    ValuePredictor(const ValuePredictor&) = delete;
    ValuePredictor& operator=(const ValuePredictor&) = delete;
    ValuePredictor(ValuePredictor&&) = delete;
    ValuePredictor& operator=(ValuePredictor&&) = delete;

    // Core functionality
    std::pair<Confidence,Value> predict(PC pc) {
        auto pred = ep.predict(pc);

        if (!lcvt.hasValue(pc) || !pred.second){
            return {Confidence::low, 0};
        }

        Value val = lcvt.lookup(pc);
        return {pred.first, val};
    }
    void updateOnBranch(InstSeqNum seqNum, bool taken){
        ep.updateOnBranch(seqNum, taken);
    }
    void onValueCommit(PC pc, Value val){
        ep.onValueCommit(pc, val == lcvt.lookup(pc));
        lcvt.update(pc, val);
    }
    void onBranchCommit(InstSeqNum seqNum){
        ep.onBranchCommit(seqNum);
    }
    void squash(InstSeqNum seqNum){
        ep.squash(seqNum);
    }

private:
    ValuePredictorParams params;
    LastCommittedValueTable lcvt;
    EqualityPredictor ep;
};

#endif // VALUE_PREDICTOR_HH