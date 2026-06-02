const std = @import("std");

fn maxarr(a: []const i32) i32 {
    var m: i32 = a[0];
    var i: usize = 1;
    while (i < a.len) : (i += 1) {
        if (a[i] > m) m = a[i];
    }
    return m;
}

pub fn main() u8 {
    const arr = [5]i32{ 1, 5, 3, 2, 4 };
    var r: i32 = 0;
    var i: i32 = 0;
    while (i < 1000000000) : (i += 1) {
        r = maxarr(&arr);
    }
    return @intCast(r & 0xFF);
}
