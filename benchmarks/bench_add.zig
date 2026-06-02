const std = @import("std");

fn add(a: i32, b: i32) i32 {
    return a + b;
}

pub fn main() u8 {
    var r: i32 = 0;
    var i: i32 = 0;
    while (i < 1000000000) : (i += 1) {
        r = add(3, 4);
    }
    return @intCast(r & 0xFF);
}
