# Class-level inheritance graph schema. Graph(), Graph.add(), Graph.list(),
# and the adjacency view are implemented by the RocksDB-backed interpreter;
# Link remains the separate instance-node edge primitive.
class Graph
    scope: list<any> := []
    adjacency: any := {list: []}
end
