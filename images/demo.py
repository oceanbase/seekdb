import pyseekdb

# Connect in embedded mode — no server needed
client = pyseekdb.Client(path="/tmp/agent_demo.db")
memory = client.get_or_create_collection("episodic")

# Write agent observations
memory.upsert(
    ids=["1", "2", "3"],
    documents=[
        "agent observed: user prefers dark mode",
        "agent observed: user speaks English and Chinese",
        "agent observed: user timezone is UTC+8",
    ],
)
print("Wrote 3 observations.\n")

# Retrieve relevant context — milliseconds after write
results = memory.query(query_texts="ui preferences?", n_results=2)
print("Query: 'ui preferences?'")
for i, doc in enumerate(results["documents"][0]):
    dist = results["distances"][0][i]
    print(f"  {i+1}. {doc}  (distance: {dist:.4f})")
