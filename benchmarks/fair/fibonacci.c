static int fib(int n){if(n<=1)return n;return fib(n-1)+fib(n-2);}
int main(){int r=0;for(int i=0;i<1000000;i++)r=r+fib(15+(i&7));return r&255;}
