; Oh freestanding runtime — self-contained memset/memcpy/memmove/memcmp.
;
; clang's loop-idiom / intrinsic lowering can synthesize calls to these C-ABI
; symbols from ordinary loops and large stores. A -nostdlib build has no libc to
; satisfy them, so we provide them ourselves and link this into every freestanding
; build. The "no-builtins" attribute stops the optimizer from turning these very
; loops back into self-referential memset/memcpy calls (infinite recursion).
;
; Portable across targets: pure typed loops over `ptr`, no arch assumptions.

define ptr @memset(ptr %d, i32 %c, i64 %n) #0 {
entry:
  %cb = trunc i32 %c to i8
  br label %cond
cond:
  %i = phi i64 [ 0, %entry ], [ %i1, %body ]
  %lt = icmp ult i64 %i, %n
  br i1 %lt, label %body, label %done
body:
  %p = getelementptr i8, ptr %d, i64 %i
  store i8 %cb, ptr %p
  %i1 = add i64 %i, 1
  br label %cond
done:
  ret ptr %d
}

define ptr @memcpy(ptr %d, ptr %s, i64 %n) #0 {
entry:
  br label %cond
cond:
  %i = phi i64 [ 0, %entry ], [ %i1, %body ]
  %lt = icmp ult i64 %i, %n
  br i1 %lt, label %body, label %done
body:
  %sp = getelementptr i8, ptr %s, i64 %i
  %v = load i8, ptr %sp
  %dp = getelementptr i8, ptr %d, i64 %i
  store i8 %v, ptr %dp
  %i1 = add i64 %i, 1
  br label %cond
done:
  ret ptr %d
}

; memmove: copy backward when the regions overlap with d > s.
define ptr @memmove(ptr %d, ptr %s, i64 %n) #0 {
entry:
  %du = ptrtoint ptr %d to i64
  %su = ptrtoint ptr %s to i64
  %fwd = icmp ule i64 %du, %su
  br i1 %fwd, label %lfwd, label %lback
lfwd:
  %fi = phi i64 [ 0, %entry ], [ %fi1, %fbody ]
  %flt = icmp ult i64 %fi, %n
  br i1 %flt, label %fbody, label %done
fbody:
  %fsp = getelementptr i8, ptr %s, i64 %fi
  %fv = load i8, ptr %fsp
  %fdp = getelementptr i8, ptr %d, i64 %fi
  store i8 %fv, ptr %fdp
  %fi1 = add i64 %fi, 1
  br label %lfwd
lback:
  %bj = phi i64 [ %n, %entry ], [ %bj1, %bbody ]
  %bgt = icmp ugt i64 %bj, 0
  br i1 %bgt, label %bbody, label %done
bbody:
  %bj1 = sub i64 %bj, 1
  %bsp = getelementptr i8, ptr %s, i64 %bj1
  %bv = load i8, ptr %bsp
  %bdp = getelementptr i8, ptr %d, i64 %bj1
  store i8 %bv, ptr %bdp
  br label %lback
done:
  ret ptr %d
}

define i32 @memcmp(ptr %a, ptr %b, i64 %n) #0 {
entry:
  br label %cond
cond:
  %i = phi i64 [ 0, %entry ], [ %i1, %body ]
  %lt = icmp ult i64 %i, %n
  br i1 %lt, label %body, label %eq
body:
  %ap = getelementptr i8, ptr %a, i64 %i
  %av = load i8, ptr %ap
  %bp = getelementptr i8, ptr %b, i64 %i
  %bv = load i8, ptr %bp
  %ne = icmp ne i8 %av, %bv
  br i1 %ne, label %diff, label %next
next:
  %i1 = add i64 %i, 1
  br label %cond
diff:
  %ai = zext i8 %av to i32
  %bi = zext i8 %bv to i32
  %r = sub i32 %ai, %bi
  ret i32 %r
eq:
  ret i32 0
}

attributes #0 = { nounwind "no-builtins" }
