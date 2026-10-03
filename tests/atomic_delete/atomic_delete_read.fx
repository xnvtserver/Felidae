# After the failed delete in atomic_delete_seed_fail.fx all three departments
# must still exist.
class Department
    key(id).
    id: string.
    kind: string.
end

def main() =>
    return Department.count().
end
