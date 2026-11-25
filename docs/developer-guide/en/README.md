# OceanBase seekdb Development Guide

## About this guide

* **The target audience** of this guide is OceanBase seekdb contributors, both new and experienced.
* **The objective** of this guide is to help contributors become an expert of OceanBase seekdb, who is familiar with its design and implementation and thus is able to use it fluently in the real world as well as develop OceanBase seekdb itself deeply.

## The structure of this guide

At present, the guide is composed of the following parts:

### 1. Get Started

This section helps you set up the development environment and get OceanBase seekdb running on your machine. Follow these steps in order if you're new to the project.

1. [Install toolchain](toolchain.md) - Install the required development tools and dependencies
2. [Get the code, build and run](build-and-run.md) - Clone the repository, build the project, and connect to the seekdb server
3. [Build for Android](android.md) - Cross-compile and run seekdb on an Android arm64-v8a device
4. [Build for iOS](ios.md) - Cross-compile seekdb into SeekDB.framework for iOS devices and simulators
5. [Build for WebAssembly](webassembly.md) - Compile seekdb with Emscripten into a WebAssembly engine and browser SQL shell
6. [Homebrew optimization](homebrew.md) - Configure mirrors when Homebrew downloads are slow

### 2. Development Basics

Once you have the environment set up, these guides will help you develop effectively:

1. [Rust refactoring guidelines](rust-refactoring.md) - Design, implementation, and acceptance requirements for Rust migrations and refactors
2. [Write and run unit tests](unittest.md) - How to write and execute unit tests
3. [Running MySQL test](mysqltest.md) - How to run MySQL compatibility tests

### 3. Contribute to OceanBase seekdb

Ready to contribute? This guide will help you get involved in the OceanBase community:

1. [Commit code and submit a pull request](contributing.md) - Step-by-step guide to contributing code changes

---

**Note**: If you're new to the project, we recommend following the sections in order. Experienced contributors can jump directly to the sections they need.
