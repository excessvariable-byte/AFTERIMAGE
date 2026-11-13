#!/usr/bin/env python3
"""Automated 50-Query Benchmark Test for AFTERIMAGE Memory Retrieval System.

Tests all 50 human recollection queries across:
1. Vague human recollections (1-10)
2. Search using remembered concepts (different wording) (11-20)
3. Conversational and messy queries (21-30)
4. PDF and OCR testing (31-40)
5. Advanced retrieval and relationship testing (41-50)
"""

import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from web.server import perform_intelligent_search

QUERIES = [
    # Group 1: Vague human recollections
    (1, "I remember reading something about object oriented programming but I forgot which file it was in."),
    (2, "Find that document where I learned about inheritance in Java."),
    (3, "I saw something explaining why normalization is needed in databases."),
    (4, "Where did I read about the difference between authentication and authorization?"),
    (5, "Find the notes where I discussed how a computer executes instructions."),
    (6, "There was something about memory management and virtual memory. Find it."),
    (7, "I remember a document explaining how classes and objects work."),
    (8, "Find the PDF where I studied different types of CPU architectures."),
    (9, "I read something about deadlocks but cannot remember its title."),
    (10, "Where is that explanation of how binary search trees work?"),

    # Group 2: Search using remembered concepts (different wording)
    (11, "Find documents explaining how a database prevents duplicate data."),
    (12, "Where did I study the process of converting source code into machine instructions?"),
    (13, "Find something about multiple threads accessing shared resources safely."),
    (14, "I need the material about how computers temporarily store frequently used data."),
    (15, "Find notes about relationships between tables in a relational database."),
    (16, "Where is the explanation of how a program handles runtime errors?"),
    (17, "Find documents about searching data efficiently using a tree structure."),
    (18, "I remember learning about probability distributions. Find those materials."),
    (19, "Find the notes explaining how a computer communicates with external devices."),
    (20, "Where did I read about how an operating system manages processes?"),

    # Group 3: Conversational and messy queries
    (21, "Hey, can you find that OOPS thing where it says OOPS means object oriented programming?"),
    (22, "I think I had a PDF on Java streams somewhere."),
    (23, "That document about SQL joins, I can't remember its name."),
    (24, "Find that thing I read about the difference between RAM and ROM."),
    (25, "I remember a page with a formula for standard deviation."),
    (26, "Can you find the file where I wrote something about multithreading?"),
    (27, "There was a diagram explaining the working of a CPU, find that."),
    (28, "I saw something about try catch finally in one of my Java files."),
    (29, "Find that statistics material with the correlation formula."),
    (30, "I don't remember the title, but it was about the third normal form."),

    # Group 4: PDF and OCR testing
    (31, "Find the PDF containing a diagram of a binary search tree."),
    (32, "Search for a scanned document containing the words data structures."),
    (33, "Find the image containing a Java inheritance diagram."),
    (34, "Find documents with a handwritten mathematical formula."),
    (35, "Search for the PDF containing a table of probability values."),
    (36, "Find the document where the heading mentions computer organization."),
    (37, "Search for a page containing a flowchart of an algorithm."),
    (38, "Find the scanned notes discussing normalization in DBMS."),
    (39, "Find a PDF containing the phrase serialization and deserialization."),
    (40, "Find the document containing a diagram of the OSI model."),

    # Group 5: Advanced retrieval and relationship testing
    (41, "Find documents that discuss both Java and multithreading."),
    (42, "Show me documents related to database normalization and SQL."),
    (43, "Find other documents connected to the topic of binary trees."),
    (44, "Which documents discuss both operating systems and memory management?"),
    (45, "Find material that connects computer networks with data communication."),
    (46, "Show me all documents that contain information about exception handling."),
    (47, "Find documents that discuss different approaches to sorting algorithms."),
    (48, "Find related notes about machine learning and neural networks."),
    (49, "Show documents that discuss both probability and hypothesis testing."),
    (50, "Find every document related to Java OOP, including documents that don't have Java in their filename.")
]


def run_benchmark():
    print("=" * 80)
    print(" AFTERIMAGE 2.0 — 50-Query Real Memory Retrieval Benchmark")
    print("=" * 80)
    print(f"Total queries to evaluate: {len(QUERIES)}")
    print()

    passed_count = 0
    start_time = time.time()
    results_summary = []

    for num, q in QUERIES:
        t0 = time.time()
        res = perform_intelligent_search(q)
        duration_ms = int((time.time() - t0) * 1000)

        interp = res.get('interpretation', {})
        results = res.get('results', [])
        total_found = res.get('total_found', 0)

        status = "PASSED" if total_found > 0 else "EMPTY"
        if total_found > 0:
            passed_count += 1
            top = results[0]
            top_title = top['title']
            top_conf = top['confidence_pct']
            top_type = top['file_type'].upper()
            top_why = top['why_selected'][:60]
            top_page = top.get('matched_page', 1)
        else:
            top_title = "None"
            top_conf = 0
            top_type = "-"
            top_why = "-"
            top_page = "-"

        results_summary.append({
            'num': num,
            'query': q,
            'status': status,
            'duration_ms': duration_ms,
            'total_found': total_found,
            'top_title': top_title,
            'top_conf': top_conf,
            'top_type': top_type,
            'top_page': top_page,
            'top_why': top_why,
            'core_terms': interp.get('core_terms', [])
        })

        # Print live status
        print(f"[{num:02d}/50] [{status:6s}] ({duration_ms:3d}ms) Q: \"{q[:50]}...\"")
        if total_found > 0:
            print(f"       -> #1: [{top_conf}% {top_type}] \"{top_title}\" (Page {top_page})")
            print(f"       -> Why: {top_why}...")

    total_duration = time.time() - start_time
    print()
    print("=" * 80)
    print(" BENCHMARK RESULTS SUMMARY")
    print("=" * 80)
    print(f"Total Queries Evaluated: {len(QUERIES)}")
    print(f"Successfully Retrieved:  {passed_count} / {len(QUERIES)} ({(passed_count/len(QUERIES))*100:.1f}%)")
    print(f"Total Execution Time:    {total_duration:.2f}s (Average {int((total_duration/len(QUERIES))*1000)}ms / query)")
    print("=" * 80)

    return passed_count == len(QUERIES), results_summary


if __name__ == '__main__':
    success, summary = run_benchmark()
    sys.exit(0 if success else 1)
