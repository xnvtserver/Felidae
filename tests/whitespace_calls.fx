import "whitespace_imported.fx".

def main() =>
    def var := 10.
    print "hello world".
    def three_arguments := function_name 10, 12, 20.
    def parenthesized := add(10, 20).
    def comma_form := add 30, 12.
    def space_form := add 20 22.
    def nested_spaces := add add 10 30, 22.
    def nested_mixed := add add(10, 30), 22.
    def nested_parenthesized := add(add(10, 30), 22).
    def imported := imported_add 19 23.
    def recursive := sum_down 4.
    (contextual_var: var, three_arguments: three_arguments,
     parenthesized: parenthesized, comma_form: comma_form,
     space_form: space_form, nested_spaces: nested_spaces,
     nested_mixed: nested_mixed,
     nested_parenthesized: nested_parenthesized,
     imported: imported, recursive: recursive).
end

def add(a: number, b: number) =>
    a + b.
end

def function_name(arg1: number, arg2: number, arg3: number) =>
    arg1 + arg2 + arg3.
end

def sum_down(n: number) =>
    n <= 0 then 0 else add(n, sum_down(n - 1)).
end

