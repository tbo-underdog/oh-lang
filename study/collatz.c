static int collatz(int n){int c=0;while(n!=1){if(n%2==0)n=n/2;else n=3*n+1;c++;}return c;}
int main(){if(collatz(27)!=111)return 1;return 0;}
