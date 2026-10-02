@missingAnnotation(label: "unknown")
def decorated() =>
    return "unreachable"
end

def main() =>
    return decorated()
end
