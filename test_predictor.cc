#include "vp.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include "tage.h"

// Test dual-counter behavior described in Section 5.1
void test_dual_counter() {
    EqualityPredictorEntry entry(0);  // tag doesn't matter for this test
    
    // Test counter initialization
    assert(entry.taken_counter == 0);
    assert(entry.not_taken_counter == 0);
    
    // Test basic counting up to nmax (7 for 3-bit counters)
    for (int i = 0; i < 10; i++) {
        entry.update(true);  // taken
        assert(entry.taken_counter <= 7);  // shouldn't exceed nmax
    }
    assert(entry.taken_counter == 7);
    
    // Test counter behavior at nmax as described in Section 5.1
    // When taken_counter is at max, not_taken should decrement
    entry.update(false);
    entry.update(false);
    assert(entry.taken_counter == 7);
    assert(entry.not_taken_counter == 2);
    
    // Test confidence levels (Section 4.1 and 4.2)
    std::cout << "Confidence is now: " << entry.getConfidence() << std::endl;
    assert(entry.getConfidence() == Confidence::high);
    
    std::cout << "Dual-counter tests passed\n";
}

// Test confidence estimation described in Section 4
void test_confidence_estimation() {
    EqualityPredictorEntry entry(0);
    
    // Test initial state (n1=0, n0=0)
    assert(entry.getConfidence() == Confidence::low);
    
    // Single occurrence (n1=1, n0=0) should give medium confidence
    entry.update(true);
    std::cout << "Confidence is now: " << entry.getConfidence() << std::endl;
    assert(entry.getConfidence() == Confidence::medium);
    
    // Test high confidence case (n1=2, n0=0)
    entry.update(true);
    assert(entry.getConfidence() == Confidence::high);
    
    // Test low confidence case (n1=3, n0=2)
    entry.update(true);
    entry.update(false);
    entry.update(false);
    assert(entry.getConfidence() == Confidence::low);

    entry.taken_counter=0;
    entry.not_taken_counter=0;
    assert(entry.getConfidence() == Confidence::low);

    entry.taken_counter=7;
    entry.not_taken_counter=3;
    assert(entry.getConfidence() == Confidence::medium);

    entry.taken_counter=5;
    entry.not_taken_counter=2;
    assert(entry.getConfidence() == Confidence::medium);

    entry.taken_counter=5;
    entry.not_taken_counter=1;
    assert(entry.getConfidence() == Confidence::high);

    
    std::cout << "Confidence estimation tests passed\n";
}

// Test the decay mechanism described in Section 5.3
void test_decay_mechanism() {
    EqualityPredictorEntry entry(0);
    
    // Build up to high confidence
    for (int i = 0; i < 4; i++) {
        entry.update(true);
    }
    assert(entry.getConfidence() == Confidence::high);
    
    // Test decay
    entry.decay();
    
    // Should maintain prediction direction but reduce confidence
    assert(entry.getDirection() == true);  // still predicts taken
    assert(entry.taken_counter < 7);  // counter should have decreased
    
    // Multiple decays should eventually reach medium confidence
    for (int i = 0; i < 5; i++) {
        entry.decay();
    }
    assert(entry.getConfidence() != Confidence::high);
    
    std::cout << "Decay mechanism tests passed\n";
}

// Test the prediction selection algorithm from Section 5.2
void test_prediction_selection() {
    std::vector<ComponentConfig> configs = {
        {256, 8, 8, 0},   // First component
        {256, 16, 8, 8},  // Second component
        {256, 32, 8, 8}   // Third component
    };
    
    EqualityPredictor pred(configs);
    PC test_pc = 0x1234;
    
    // Initial prediction should be false (no history)
    assert(pred.predict(test_pc).second == false);
    
    std::cout << "Prediction selection tests passed\n";
}

// Test allocation policy from Section 5.5
void test_allocation_policy() {
    std::vector<ComponentConfig> configs = {
        {256, 8, 8, 0},
        {256, 16, 8, 8},
        {256, 32, 8, 8}
    };
    
    EqualityPredictor pred(configs);
    PC test_pc = 0x1234;
    
    // Should allocate on misprediction
    std::cout << "Starting the thingy magic" << std::endl;
    std::cout << "1) COMMITTING TRUE:" << std::endl;
    pred.onValueCommit(test_pc, true);
    std::cout << "2) COMMITTING TRUE:" << std::endl;
    pred.onValueCommit(test_pc, true);
    std::cout << "3) COMMITTING FALSE:" << std::endl;
    pred.onValueCommit(test_pc, false);
    std::cout << "4) COMMITTING FALSE:" << std::endl;
    pred.onValueCommit(test_pc, false);
    std::cout << "5) COMMITTING FALSE:" << std::endl;
    pred.onValueCommit(test_pc, false);
    std::cout << "6) COMMITTING FALSE:" << std::endl;
    pred.onValueCommit(test_pc, false);
    
    // Next prediction should reflect the allocated entry
    assert(pred.predict(test_pc).second == false);
    
    std::cout << "Allocation policy tests passed\n";
}

void test_path_folding() {
    auto pt = PathTracker(5, 2, 3);
    pt.addBranch(true);
    pt.addBranch(true);
    assert(pt.folded_path == 3);
    pt.addBranch(true);
    pt.revertBranches(2);
    assert(pt.folded_path == 1);

    for(int i=0; i<100; i++){
        pt.addBranch(true);
    }

    pt.revertBranches(1);
    pt.revertBranches(1);
    assert(pt.folded_path==31);
}

// Test speculative state handling
void test_speculative_state() {
    std::vector<ComponentConfig> configs = {
        {256, 2, 8, 0},
        {256, 4, 8, 8}
    };
    
    EqualityPredictor pred(configs);
    PC test_pc = 0x1234;
    
    // Create speculative state
    for(int i=0; i<10; i++){
        bool b = (i%2==0);
        pred.updateOnBranch(i*2, b);
        pred.updateOnBranch(i*2+1, b);
        pred.onValueCommit(test_pc, b);
    }
    pred.updateOnBranch(30, true);
    pred.updateOnBranch(40, true);

    pred.updateOnBranch(50, false);
    pred.updateOnBranch(60, false);

    assert(pred.predict(test_pc).second == false);
    pred.squash(50);
    assert(pred.predict(test_pc).second == true);
    
    std::cout << "Speculative state tests passed\n";
}

void test_convergence_to_high_confidence() {
    std::vector<ComponentConfig> configs = {
        {256, 0, 8, 0},
        {256, 4, 8, 8}
    };
    EqualityPredictor pred(configs);
    PC test_pc = 0x1000;

    std::cout << "BEGINNING PROCESS!" << std::endl;

    // Test if we can learn a simple pattern
    // If the hist is ...11, output 1, else 0
    double correct = 0;
    double wrong = 0;

    bool prev=false;
    for (int i = 0; i < 50000; i++) {
        bool n = (rand()%2==0);
        pred.updateOnBranch(0, n);
        pred.onBranchCommit(0);

        bool v = (prev && n);
        prev = n;

        auto [_, p] = pred.predict(test_pc);

        if (p==(v)){
            correct++;
        } else {
            wrong++;
        }

        pred.onValueCommit(test_pc, (v));
    }

    std::cout << "Training done. Accuracy: " << (correct/(correct+wrong)) << std::endl;
    assert(correct/(correct+wrong)>0.99);
    // After training, check prediction is always taken and high confidence

    pred.updateOnBranch(0, 0);
    pred.updateOnBranch(1, 1);
    pred.updateOnBranch(1, 1);
    auto [confComp, final_prediction] = pred.predict(test_pc);
    assert(final_prediction == true);
    assert(confComp == Confidence::high);

    auto predictingEntry = pred.predictingEntry(test_pc).value().get();
    assert(predictingEntry.taken_counter == 7 );
    assert(predictingEntry.not_taken_counter == 0 );

    std::cout << "Convergence to high confidence test passed\n";
}

void test_alternating_pattern() {
    std::vector<ComponentConfig> configs = {
        {256, 8, 8, 0},
        {256, 16, 8, 8}
    };
    EqualityPredictor pred(configs);
    PC test_pc = 0x2000;

    // 100 updates of alternating pattern
    for (int i = 0; i < 100; i++) {
        bool outcome = (i % 2 == 0); // true, false, true, false...
        pred.onValueCommit(test_pc, outcome);
    }

    auto [conf, prediction] = pred.predict(test_pc);
    assert(conf != Confidence::high);

    std::cout << "Alternating pattern test passed\n";
}

void test_rapid_pattern_shift() {
    std::vector<ComponentConfig> configs = {
        {256, 8, 8, 0},
        {256, 16, 8, 8}
    };
    EqualityPredictor pred(configs);
    PC test_pc = 0x3000;

    // Phase 1: always taken (50 times)
    for (int i = 0; i < 50; i++) {
        pred.onValueCommit(test_pc, true);
    }

    // Verify it’s stable on taken
    auto [_, pred_before_shift] = pred.predict(test_pc);
    assert(pred_before_shift == true);

    // Phase 2: always not-taken (50 times)
    int mispred_count = 0;
    for (int i = 0; i < 50; i++) {
        auto [_, p] = pred.predict(test_pc);
        if (p != false) mispred_count++;
        pred.onValueCommit(test_pc, false);
    }

    // Should have adapted somewhat.
    auto [__, pred_after_shift] = pred.predict(test_pc);
    assert(pred_after_shift == false);

    std::cout << "Rapid pattern shift test passed. Mispredictions: " << mispred_count << "\n";
}

void test_decay_from_high_to_medium() {
    std::vector<ComponentConfig> configs = {
        {256, 8, 8, 0},
        {256, 16, 8, 8}
    };
    EqualityPredictor pred(configs);
    PC test_pc = 0x4000;

    // Build to high confidence always-taken
    for (int i = 0; i < 10; i++) {
        pred.onValueCommit(test_pc, true);
    }

    // Now apply a pattern that doesn't re-affirm the taken prediction:
    // For example, feed the predictor with not-taken outcomes or call decay directly if exposed.
    for (int i = 0; i < 5; i++) {
        pred.onValueCommit(test_pc, false); 
        // This should cause entries to be updated and possibly decay others.
    }

    assert(pred.getPredictingEntries(test_pc).primary.value().get().getConfidence() != Confidence::high);

    std::cout << "Decay from high to medium confidence test passed\n";
}

void test_accuracy_on_trace() {
    std::vector<ComponentConfig> configs = {
        {2048, 0, 11, 0},
        {512, 2, 9, 12},
        {512, 4, 9, 12},
        {512, 8, 9, 12},
        {512, 16, 9, 12},
        {512, 32, 9, 12},
        {512, 64, 9, 12},
        {512, 128, 9, 12}
    };
    EqualityPredictor eq(configs);

    // Instantiate TAGE predictor
    tage_init();

    // Tracking stats for EqualityPredictor
    long eq_correct = 0;
    long eq_wrong = 0;
    long eq_total = 0;

    // Tracking stats for TAGE predictor
    long tage_correct = 0;
    long tage_wrong = 0;
    long tage_total = 0;

    std::ifstream file("trace_gcc.txt");
    if (!file.is_open()) {
        std::cerr << "Error: Could not open trace_gcc.txt\n";
        return;
    }
    
    std::string address_str, outcome_str;
    while (file >> address_str >> outcome_str) {
        uint64_t address = std::stoull(address_str, nullptr, 16);
        bool taken = (outcome_str == "t");

        // Get prediction from EqualityPredictor
        auto [conf, eq_prediction] = eq.predict(address);

        // Get prediction from TAGE
        uint8_t tage_pred_value = tage_predict((uint32_t)address); 
        bool tage_prediction = (tage_pred_value == TAKEN);

        // Update EqualityPredictor with outcome
        eq.onValueCommit(address, taken);
        eq.updateOnBranch(0, taken);
        eq.onBranchCommit(0);

        // Update TAGE predictor with outcome
        tage_train((uint32_t)address, taken ? TAKEN : NOTTAKEN);

        // Update stats for EqualityPredictor
        if (eq_prediction == taken) {
            eq_correct++;
        } else {
            eq_wrong++;
        }
        eq_total++;

        // Update stats for TAGE predictor
        if (tage_prediction == taken) {
            tage_correct++;
        } else {
            tage_wrong++;
        }
        tage_total++;

        if (eq_total % 100000 == 0) {
            double eq_accuracy = static_cast<double>(eq_correct) / eq_total;
            double eq_mpki = static_cast<double>(eq_wrong) / eq_total * 1000;

            double tage_accuracy = static_cast<double>(tage_correct) / tage_total;
            double tage_mpki = static_cast<double>(tage_wrong) / tage_total * 1000;

            std::cout << "Processed " << eq_total << " branches\n";
            std::cout << "EqualityPredictor: Accuracy: " << eq_accuracy 
                      << ", MPKI: " << eq_mpki << "\n";
            std::cout << "TAGE: Accuracy: " << tage_accuracy 
                      << ", MPKI: " << tage_mpki << "\n";
        }
    }

    file.close();

    // Final accuracy results
    double final_eq_accuracy = static_cast<double>(eq_correct) / eq_total;
    double final_eq_mpki = static_cast<double>(eq_wrong) / eq_total * 1000;
    double final_tage_accuracy = static_cast<double>(tage_correct) / tage_total;
    double final_tage_mpki = static_cast<double>(tage_wrong) / tage_total * 1000;

    std::cout << "\nFinal results:\n";
    std::cout << "EqualityPredictor -> Accuracy: " << final_eq_accuracy 
              << ", MPKI: " << final_eq_mpki << "\n";
    std::cout << "TAGE -> Accuracy: " << final_tage_accuracy 
              << ", MPKI: " << final_tage_mpki << "\n";
}


int main() {
    test_dual_counter();
    test_confidence_estimation();
    test_decay_mechanism();
    test_prediction_selection();
    test_allocation_policy();
    test_path_folding();
    test_speculative_state();

    test_convergence_to_high_confidence();
    test_alternating_pattern();
    test_rapid_pattern_shift();
    test_decay_from_high_to_medium();
    
    test_accuracy_on_trace();

    std::cout << "All BATAGE specific tests passed!\n";
    return 0;
}