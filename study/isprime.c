static int isprime(int n){if(n<2)return 0;for(int i=2;i*i<=n;i++)if(n%i==0)return 0;return 1;}
int main(){if(isprime(97)!=1)return 1;if(isprime(91)!=0)return 2;if(isprime(2)!=1)return 3;return 0;}
