#include <stdint.h>
static void dijkstra(int32_t *adj, int32_t *dist, int n, int src) {
    int32_t vis[8] = {0};
    for (int i = 0; i < n; i++) dist[i] = 999999;
    dist[src] = 0;
    for (int iter = 0; iter < n; iter++) {
        int u = -1;
        for (int i = 0; i < n; i++)
            if (!vis[i] && (u == -1 || dist[i] < dist[u])) u = i;
        if (u == -1) return;
        vis[u] = 1;
        for (int v = 0; v < n; v++) {
            int w = adj[u*n+v];
            if (w > 0 && dist[u]+w < dist[v]) dist[v] = dist[u]+w;
        }
    }
}
int main(void) {
    int32_t adj[36] = {0,4,1,0,0,0, 0,0,0,1,0,10, 0,2,0,5,0,0, 0,0,0,0,3,0, 0,0,0,0,0,1, 0,0,0,0,0,0};
    int32_t dist[8];
    for (int r = 0; r < 1000000; r++) dijkstra(adj, dist, 6, 0);
    return dist[5] == 8 ? 0 : 1;
}
