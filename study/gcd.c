static int gcd(int a,int b){return b==0?a:gcd(b,a%b);}
int main(){if(gcd(48,36)!=12)return 1;if(gcd(17,5)!=1)return 2;return 0;}
