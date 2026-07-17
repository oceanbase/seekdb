import pyseekdb

client = pyseekdb.Client(path="./agent_state.db")
memory = client.get_or_create_collection(name="episodic")

# Round 1: write Agent observations
print("--- Round 1: write 3 observations ---")
memory.upsert(
    ids=["1", "2", "3"],
    documents=[
        "user prefers dark mode",
        "user speaks English and Chinese",
        "user timezone is UTC+8",
    ],
)
memory.refresh_index()

print('Query: "ui preferences?"')
results = memory.query(query_texts="ui preferences?", n_results=2)
for doc, dist in zip(results["documents"][0], results["distances"][0]):
    print(f"  {doc}  (distance: {dist:.4f})")

# Round 2: write new observation, queryable immediately
print("\n--- Round 2: write 1 new observation ---")
memory.upsert(ids=["4"], documents=["user saw pricing page 3 times today"])
memory.refresh_index()

print('Query: "purchase intent signals"')
results = memory.query(query_texts="purchase intent signals", n_results=1)
print("Result:", results["documents"])
