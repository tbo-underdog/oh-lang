static int slen(char*s){int i=0;while(s[i])i++;return i;}
int main(){if(slen("hello")!=5)return 1;return 0;}
