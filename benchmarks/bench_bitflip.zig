fn flipbits(x: i32) i32 { return ~x; }
fn popcount(x: i32) i32 {
    var c: i32 = 0;
    var b: i32 = 0;
    while (b < 32) : (b += 1) {
        const m: i32 = @as(i32,1) << @intCast(b);
        if ((x & m) == m) c += 1;
    }
    return c;
}
pub fn main() u8 {
    const arr = [8]i32{5,3,8,1,9,2,7,4};
    var ok: bool = true;
    var r: usize = 0;
    while (r < 1000000) : (r += 1) {
        for (arr) |v| if (popcount(v) + popcount(flipbits(v)) != 32) { ok = false; };
    }
    return if (ok) 0 else 1;
}
