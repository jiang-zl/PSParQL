#ifndef GMATCHSUBGRAPH_H_
#define GMATCHSUBGRAPH_H_

#include "subgraph/Vertex.h"
#include "subgraph/Subgraph.h"

struct AdjItem
{
  VertexID id;
  Label l;

  EdgeDirect d;
  EdgeLabel el;

  bool operator==(const AdjItem &rhs) const
  {
    bool check1 = (id == rhs.id);
    bool check2 = (l == rhs.l);
    bool check3 = (d == rhs.d);
    bool check4 = (el == rhs.el);

    return (check1 && check2 && check3 && check4);
  }
};

struct AdjEdge
{
  Label l;
  EdgeDirect d;
  EdgeLabel el;

  AdjEdge(Label _l, EdgeDirect _d, EdgeLabel _el) : l(_l), d(_d), el(_el) { }

  AdjEdge(const AdjItem &adj) : l(adj.l), d(adj.d), el(adj.el) { }

  bool operator==(const AdjEdge &rhs) const
  {
    bool check1 = (l == rhs.l);
    bool check2 = (d == rhs.d);
    bool check3 = (el == rhs.el);

    return (check1 && check2 && check3);
  }
};

struct AdjEdgeHash
{
  std::size_t operator()(const AdjEdge &adj) const 
  {
    std::hash<Label> h1;
    std::hash<EdgeDirect> h2;
    std::hash<EdgeLabel> h3;
    size_t seed = h1(adj.l);
    seed ^= h2(adj.d) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= h3(adj.el) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct AdjItemHash
{
  std::size_t operator()(const AdjItem &pAdjItem) const
  {
    std::hash<VertexID> h1;
    size_t seed = h1(pAdjItem.id);
    std::hash<Label> h2;
    seed ^= h2(pAdjItem.l) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    std::hash<EdgeDirect> h3;
    seed ^= h3(pAdjItem.d) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    std::hash<EdgeLabel> h4;
    seed ^= h4(pAdjItem.el) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
  }
};

struct GMatchValue
{
  Label l;
  vector<AdjItem> adj;            // 邻接表
  vector<VertexID> matched_query; // 可能匹配到的查询点

  int inAdjSize;
  int outAdjSize;
  GMatchValue() : l(0), inAdjSize(0), outAdjSize(0) { }
};

typedef Vertex<VertexID, GMatchValue> GMatchVertex;
typedef Subgraph<GMatchVertex> GMatchSubgraph;

struct vertex_hash
{
  std::size_t operator()(const GMatchVertex *p) const
  {
    return p->id;
  }
};

inline bool adj_comp(const AdjItem &a, const AdjItem &b)
{
  return a.id < b.id;
}

// 以下函数是对 >> 、<< 运算符的重载，在序列化对象时使用
obinstream &operator>>(obinstream &m, AdjItem &v)
{
  m >> v.id;
  m >> v.l;
  m >> v.d;
  m >> v.el;
  return m;
}

ibinstream &operator<<(ibinstream &m, const AdjItem &v)
{
  m << v.id;
  m << v.l;
  m << v.d;
  m << v.el;
  return m;
}

ofbinstream &operator>>(ofbinstream &m, AdjItem &v)
{
  m >> v.id;
  m >> v.l;
  m >> v.d;
  m >> v.el;
  return m;
}

ifbinstream &operator<<(ifbinstream &m, const AdjItem &v)
{
  m << v.id;
  m << v.l;
  m << v.d;
  m << v.el;
  return m;
}

//--------- GMatchValue ---------
obinstream &operator>>(obinstream &m, GMatchValue &Val)
{
  m >> Val.l;
  m >> Val.adj;
  m >> Val.matched_query;
  m >> Val.inAdjSize;
  m >> Val.outAdjSize;
  return m;
}

ibinstream &operator<<(ibinstream &m, const GMatchValue &Val)
{
  m << Val.l;
  m << Val.adj;
  m << Val.matched_query;
  m << Val.inAdjSize;
  m << Val.outAdjSize;
  return m;
}

ofbinstream &operator>>(ofbinstream &m, GMatchValue &Val)
{
  m >> Val.l;
  m >> Val.adj;
  m >> Val.matched_query;
  m >> Val.inAdjSize;
  m >> Val.outAdjSize;
  return m;
}

ifbinstream &operator<<(ifbinstream &m, const GMatchValue &Val)
{
  m << Val.l;
  m << Val.adj;
  m << Val.matched_query;
  m << Val.inAdjSize;
  m << Val.outAdjSize;
  return m;
}

#endif