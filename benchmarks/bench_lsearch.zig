fn lsearch(a: []const i32, val: i32) i32 {
    for (a, 0..) |x, i| if (x == val) return @intCast(i);
    return -1;
}
pub fn main() u8 {
    const a = [10]i32{3,7,1,9,5,2,8,4,6,0};
    var r: i32 = 0;
    var i: usize = 0;
    while (i < 10000000) : (i += 1) r += lsearch(&a, 5);
    return if (r == 40000000) 0 else 1;
}
