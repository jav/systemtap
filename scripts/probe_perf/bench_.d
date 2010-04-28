provider bench
{
 probe permute(long cycles,int arg1);
 probe tower(long cycles,int arg1, int arg2, int arg3);
 probe try(long cycles,int i, int q, int a, int b, int c, int x);
 probe innerproduct(long cycles,int a, int b, int row, int column);
 probe trial(long cycles,int i, int j, int k);
 probe quicksort(long cycles,int i, int j, int x);
 probe bubble(long cycles,int i, int top);
 probe insert(long cycles,int n, int t);
 probe fft(long cycles,int n, int z, int w, int e);
}

