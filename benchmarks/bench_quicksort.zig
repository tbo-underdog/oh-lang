fn qswap(a: []i32, i: usize, j: usize) void { const t=a[i]; a[i]=a[j]; a[j]=t; }
fn qpart(a: []i32, lo: usize, hi: usize) usize {
    const p=a[hi]; var i: usize = if (lo>0) lo-1 else 0; var has_i = lo>0;
    var j: usize = lo;
    while (j < hi) : (j += 1) {
        if (a[j] <= p) { if (has_i) { i+=1; } else { has_i=true; } qswap(a,i,j); }
    }
    const ni = if (has_i) i+1 else 0; qswap(a,ni,hi); return ni;
}
fn qsort_arr(a: []i32, lo: usize, hi: usize) void {
    if (lo<hi) { const p=qpart(a,lo,hi); if(p>0) qsort_arr(a,lo,p-1); qsort_arr(a,p+1,hi); }
}
pub fn main() u8 {
    const src = [16]i32{9,3,7,1,0,8,4,2,6,5,15,11,13,10,12,14};
    var a = [16]i32{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    var r: usize = 0;
    while (r < 1000000) : (r += 1) {
        for (&a, 0..) |*x, i| x.* = src[i];
        qsort_arr(&a, 0, 15);
    }
    return if (a[0]==0 and a[15]==15) 0 else 1;
}
