static int atoi2(char*s){int n=0,i=0;while(s[i]>=48&&s[i]<=57){n=n*10+s[i]-48;i++;}return n;}
int main(){if(atoi2("12345")!=12345)return 1;return 0;}
