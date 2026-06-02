const std = @import("std");

fn sum(n: i32) i32 {
    var s: i32 = 0;
    var i: i32 = 1;
    while (i <= n) : (i += 1) {
        s += i;
    }
    return s;
}

pub fn main() u8 {
    var r: i32 = 0;
    var j: i32 = 0;
    while (j < 10000000) : (j += 1) {
        r = sum(100);
    }
    return @intCast(r & 0xFF);
}
