fn dijkstra(adj: []const i32, dist: []i32, n: i32, src: i32) void {
    var vis = [8]i32{ 0, 0, 0, 0, 0, 0, 0, 0 };
    var i: i32 = 0;
    while (i < n) : (i += 1) dist[@intCast(i)] = 999999;
    dist[@intCast(src)] = 0;
    var iter: i32 = 0;
    while (iter < n) : (iter += 1) {
        var u: i32 = -1;
        var j: i32 = 0;
        while (j < n) : (j += 1)
            if (vis[@intCast(j)] == 0 and (u == -1 or dist[@intCast(j)] < dist[@intCast(u)])) { u = j; };
        if (u == -1) return;
        vis[@intCast(u)] = 1;
        var v: i32 = 0;
        while (v < n) : (v += 1) {
            const w = adj[@intCast(u*n+v)];
            if (w > 0 and dist[@intCast(u)]+w < dist[@intCast(v)]) dist[@intCast(v)] = dist[@intCast(u)]+w;
        }
    }
}
pub fn main() u8 {
    const adj = [36]i32{0,4,1,0,0,0, 0,0,0,1,0,10, 0,2,0,5,0,0, 0,0,0,0,3,0, 0,0,0,0,0,1, 0,0,0,0,0,0};
    var dist = [8]i32{ 0, 0, 0, 0, 0, 0, 0, 0 };
    var r: i32 = 0;
    while (r < 1000000) : (r += 1) dijkstra(&adj, &dist, 6, 0);
    return if (dist[5] == 8) 0 else 1;
}
