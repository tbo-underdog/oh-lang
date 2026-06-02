const std = @import("std");

fn fib(n: i32) i32 {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

pub fn main() u8 {
    var r: i32 = 0;
    var i: i32 = 0;
    while (i < 10000) : (i += 1) {
        r = fib(30);
    }
    return @intCast(r & 0xFF);
}
