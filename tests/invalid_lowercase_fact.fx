# A fact is a class constructor call, so its name must begin with an uppercase
# letter. A lowercase name is rejected at parse time.
ghost(a: 1).

def main() =>
    return 1.
end
