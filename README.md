# Bayesian Last Committed Value Predictor

This repository implements a Bayesian Last Committed Value Predictor, inspired by the BATAGE branch predictor (without doing controlled allocation throttling).

## Overview

- **vp.h**: Core logic for the Bayesian Last Committed Value Predictor.
- **test_predictor.cc**: Test suite for validation and correctness checks.
- **tage.h**: Forked from the [CSE240-Branch-Predictor repository](https://github.com/pwwpche/CSE240-Branch-Predictor). Serves as a baseline equality predictor for comparison.
- **trace_gcc.txt**: Trace file used to verify and compare performance against the equality predictor.