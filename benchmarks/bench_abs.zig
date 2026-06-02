const std = @import("std");

fn absi(x: i32) i32 {
    return if (x < 0) -x else x;
}

pub fn main() u8 {
    var r: i32 = 0;
    var i: i32 = -500000000;
    while (i < 500000000) : (i += 1) {
        r = absi(i);
    }
    return @intCast(r & 0xFF);
}
