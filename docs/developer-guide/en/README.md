# OceanBase SeekDB Development Guide

## About this guide

* **The target audience** of this guide is OceanBase SeekDB contributors, both new and experienced.
* **The objective** of this guide is to help contributors become an expert of OceanBase SeekDB, who is familiar with its design and implementation and thus is able to use it fluently in the real world as well as develop OceanBase SeekDB itself deeply.

## The structure of this guide

At present, the guide is composed of the following parts:

### 1. Get Started

This section helps you set up the development environment and get OceanBase SeekDB running on your machine. Follow these steps in order if you're new to the project.

1. [Install toolchain](toolchain.md) - Install the required development tools and dependencies
2. [Get the code, build and run](build-and-run.md) - Clone the repository, build the project, and connect to the seekdb server

### 2. Development Basics

Once you have the environment set up, these guides will help you develop effectively:

1. [Set up an IDE](ide-settings.md) - Configure your development environment for optimal productivity
2. [Coding Conventions](coding-convention.md) - Learn OceanBase SeekDB's programming habits and conventions
3. [Coding Standard](coding-standard.md) - Detailed C++ coding standards and constraints
4. [Write and run unit tests](unittest.md) - How to write and execute unit tests
5. [Running MySQL test](mysqltest.md) - How to run MySQL compatibility tests
6. [Debug](debug.md) - Debugging techniques and tools

### 3. Understanding the Codebase

Before you start developing a big feature, it's recommended to read these documents to better understand OceanBase SeekDB's internals:

1. [Logging System](logging.md) - How logging works in OceanBase SeekDB
2. [Memory Management](memory.md) - Memory management strategies and best practices
3. [Basic Data Structures](container.md) - Core data structures used in the codebase

### 4. Contribute to OceanBase SeekDB

Ready to contribute? This guide will help you get involved in the OceanBase community:

1. [Commit code and submit a pull request](contributing.md) - Step-by-step guide to contributing code changes

---

**Note**: If you're new to the project, we recommend following the sections in order. Experienced contributors can jump directly to the sections they need.