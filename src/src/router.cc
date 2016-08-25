#include "router.h"
#include "util.h"
#include "gurobi_c++.h"
#include "smoother.h"
#include "subsetsum4.h"
#include "undirected_graph.h"
#include <cstdio>
#include <algorithm>
#include <set>

router::router(int r, splice_graph &g, MEI &ei, VE &ie)
	:root(r), gr(g), e2i(ei), i2e(ie)
{
	touched = true;
}

router::router(int r, splice_graph &g, MEI &ei, VE &ie, GRBEnv *_env)
	:root(r), gr(g), e2i(ei), i2e(ie), env(_env)
{
	touched = true;
}

/*
router::router(const router &rt)
	:root(rt.root), gr(rt.gr), e2i(rt.e2i), i2e(rt.i2e), env(rt.env), touched(rt.touched)
{
}
*/

router& router::operator=(const router &rt)
{
	touched = rt.touched;
	env = rt.env;

	root = rt.root;
	gr = rt.gr;
	e2i = rt.e2i;
	i2e = rt.i2e;

	routes = rt.routes;
	counts = rt.counts;
	
	e2u = rt.e2u;
	u2e = rt.u2e;

	ratio = rt.ratio;
	eqns = rt.eqns;

	return (*this);
}

int router::update()
{
	//if(touched == false) return 0;
	update_routes();
	build_indices();
	divide();
	evaluate();
	touched = false;
	return 0;
}

int router::divide()
{
	eqns.clear();
	ratio = -1;
	if(gr.in_degree(root) >= 2 && gr.out_degree(root) >= 2)
	{
		run_subsetsum();
	}
	else if(gr.in_degree(root) >= 1 && gr.out_degree(root) >= 1)
	{
		add_single_equation();
	}
	return 0;
}

int router::add_single_equation()
{
	equation eqn;
	for(int i = 0; i < gr.in_degree(root); i++) eqn.s.push_back(u2e[i]);
	for(int i = gr.in_degree(root); i < gr.degree(root); i++) eqn.t.push_back(u2e[i]);
	assert(eqn.s.size() >= 1);
	assert(eqn.t.size() >= 1);
	eqns.push_back(eqn);
	return 0;
}

int router::run_subsetsum()
{
	undirected_graph ug;
	for(int i = 0; i < u2e.size(); i++) ug.add_vertex();
	for(int i = 0; i < routes.size(); i++)
	{
		int e1 = routes[i].first;
		int e2 = routes[i].second;
		assert(e2u.find(e1) != e2u.end());
		assert(e2u.find(e2) != e2u.end());
		int s = e2u[e1];
		int t = e2u[e2];
		assert(s >= 0 && s < gr.in_degree(root));
		assert(t >= gr.in_degree(root) && t < gr.degree(root));
		ug.add_edge(s, t);
	}

	vector< set<int> > vv = ug.compute_connected_components();

	vector<PI> ss;
	vector<PI> tt;
	for(int i = 0; i < vv.size(); i++)
	{
		double ww = 0;
		for(set<int>::iterator it = vv[i].begin(); it != vv[i].end(); it++)
		{
			edge_descriptor e = i2e[u2e[*it]];
			assert(e != null_edge);
			double w = gr.get_edge_weight(e);
			if(e->source() == root) w = 0 - w;
			else assert(e->target() == root);
			ww += w;
		}
		if(ww >= 0) ss.push_back(PI((int)(ww), i));
		else tt.push_back(PI((int)(0 - ww), i));
	}

	if(ss.size() <= 1 || tt.size() <= 1) return 0;

	subsetsum4 sss(ss, tt);
	sss.solve();

	equation eqn1;
	for(int i = 0; i < sss.eqn.s.size(); i++)
	{
		int k = sss.eqn.s[i];
		for(set<int>::iterator it = vv[k].begin(); it != vv[k].end(); it++)
		{
			int e = *it;
			if(e < gr.in_degree(root)) eqn1.s.push_back(u2e[e]);
			else eqn1.t.push_back(u2e[e]);
		}
	}
	for(int i = 0; i < sss.eqn.t.size(); i++)
	{
		int k = sss.eqn.t[i];
		for(set<int>::iterator it = vv[k].begin(); it != vv[k].end(); it++)
		{
			int e = *it;
			if(e < gr.in_degree(root)) eqn1.s.push_back(u2e[e]);
			else eqn1.t.push_back(u2e[e]);
		}
	}

	set<int> s1(eqn1.s.begin(), eqn1.s.end());
	set<int> s2(eqn1.t.begin(), eqn1.t.end());
	equation eqn2;
	for(int i = 0; i < gr.in_degree(root); i++)
	{
		int e = u2e[i];
		if(s1.find(e) != s1.end()) continue;
		eqn2.s.push_back(e);
	}
	for(int i = gr.in_degree(root); i < gr.degree(root); i++)
	{
		int e = u2e[i];
		if(s2.find(e) != s2.end()) continue;
		eqn2.t.push_back(e);
	}

	eqns.push_back(eqn1);
	eqns.push_back(eqn2);

	return 0;
}

int router::run_ilp1()
{
	//GRBEnv *env = new GRBEnv();
	GRBModel *model = new GRBModel(*env);

	// ratio variables
	GRBVar xvar = model->addVar(1, GRB_INFINITY, 1, GRB_CONTINUOUS);
	model->update();

	// partition and weights variables
	vector<GRBVar> bvars;
	vector<GRBVar> pvars;
	vector<GRBVar> qvars;
	for(int i = 0; i < e2u.size(); i++)
	{
		GRBVar bvar = model->addVar(0, 1, 0, GRB_BINARY);
		GRBVar pvar = model->addVar(0, GRB_INFINITY, 0, GRB_CONTINUOUS);
		GRBVar qvar = model->addVar(0, GRB_INFINITY, 0, GRB_CONTINUOUS);
		bvars.push_back(bvar);
		pvars.push_back(pvar);
		qvars.push_back(qvar);
	}
	model->update();

	// constraints from routes
	for(int i = 0; i < routes.size(); i++)
	{
		int u1 = e2u[routes[i].first];
		int u2 = e2u[routes[i].second];
		assert(u1 >= 0 && u1 < bvars.size());
		assert(u2 >= 0 && u2 < bvars.size());
		model->addConstr(bvars[u1], GRB_EQUAL, bvars[u2]);
	}

	// guarantee a non-trivial partition
	GRBLinExpr exp1 = 0, exp2 = 0, exp3 = 0, exp4 = 0;
	for(int i = 0; i < gr.in_degree(root); i++)
	{
		exp1 += bvars[i];
		exp2 += (1 - bvars[i]);
	}
	for(int i = gr.in_degree(root); i < gr.degree(root); i++)
	{
		exp3 += bvars[i];
		exp4 += (1 - bvars[i]);
	}
	model->addConstr(exp1, GRB_GREATER_EQUAL, 1);
	model->addConstr(exp2, GRB_GREATER_EQUAL, 1);
	model->addConstr(exp3, GRB_GREATER_EQUAL, 1);
	model->addConstr(exp4, GRB_GREATER_EQUAL, 1);

	// lower bounds and upper bounds
	double ubound = 0.0;
	vector<double> weights;
	for(int i = 0; i < u2e.size(); i++)
	{
		edge_descriptor e = i2e[u2e[i]];
		double w = gr.get_edge_weight(e);
		weights.push_back(w);
		ubound += w;
	}
	for(int i = 0; i < bvars.size(); i++)
	{
		//printf("weight at %d = %.2lf, ubound = %.2lf\n", i, weights[i], ubound);
		model->addConstr(pvars[i], GRB_GREATER_EQUAL, bvars[i] * weights[i]);
		model->addConstr(pvars[i], GRB_LESS_EQUAL, bvars[i] * ubound);
		model->addConstr(pvars[i], GRB_LESS_EQUAL, xvar * weights[i]);

		model->addConstr(qvars[i], GRB_GREATER_EQUAL, (1 - bvars[i]) * weights[i]);
		model->addConstr(qvars[i], GRB_LESS_EQUAL, (1 - bvars[i]) * ubound);
		model->addConstr(qvars[i], GRB_LESS_EQUAL, xvar * weights[i]);
	}

	// balance
	exp1 = exp2 = exp3 = exp4 = 0;
	for(int i = 0; i < gr.in_degree(root); i++)
	{
		exp1 += pvars[i];
		exp2 += qvars[i];
	}
	for(int i = gr.in_degree(root); i < gr.degree(root); i++)
	{
		exp3 += pvars[i];
		exp4 += qvars[i];
	}
	model->addConstr(exp1, GRB_EQUAL, exp3);
	model->addConstr(exp2, GRB_EQUAL, exp4);

	// set objective and optimize
	GRBLinExpr obj = xvar;
	model->setObjective(obj, GRB_MINIMIZE);
	model->getEnv().set(GRB_IntParam_OutputFlag, 0);
	model->update();

	model->optimize();

	int f = model->get(GRB_IntAttr_Status);

	if(f == GRB_OPTIMAL)
	{
		equation p, q;
		for(int i = 0; i < bvars.size(); i++)
		{
			/*
			double b0 = bvars[i].get(GRB_DoubleAttr_X);
			double p = pvars[i].get(GRB_DoubleAttr_X);
			double q = qvars[i].get(GRB_DoubleAttr_X);
			printf("b = %e, p = %e, q = %e\n", b0, p, q);
			if(b == 1) assert(q <= 0.001);
			if(b == 0) assert(p <= 0.001);
			*/
			int b = (bvars[i].get(GRB_DoubleAttr_X) >= 0.5) ? 1 : 0;
			if(b == 1 && i < gr.in_degree(root)) p.s.push_back(u2e[i]);
			if(b == 1 && i >=gr.in_degree(root)) p.t.push_back(u2e[i]);
			if(b == 0 && i < gr.in_degree(root)) q.s.push_back(u2e[i]);
			if(b == 0 && i >=gr.in_degree(root)) q.t.push_back(u2e[i]);
		}
		eqns.push_back(p);
		eqns.push_back(q);
	}

	delete model;
	//delete env;
	return 0;
}

int router::run_ilp2()
{
	GRBModel *model = new GRBModel(*env);

	// ratio variables
	GRBVar xvar = model->addVar(1, GRB_INFINITY, 1, GRB_CONTINUOUS);
	model->update();

	// partition and weights variables
	vector<GRBVar> bvars;
	for(int i = 0; i < e2u.size(); i++)
	{
		GRBVar bvar = model->addVar(0, 1, 0, GRB_BINARY);
		bvars.push_back(bvar);
	}
	model->update();

	// constraints from routes
	for(int i = 0; i < routes.size(); i++)
	{
		int u1 = e2u[routes[i].first];
		int u2 = e2u[routes[i].second];
		assert(u1 >= 0 && u1 < bvars.size());
		assert(u2 >= 0 && u2 < bvars.size());
		model->addConstr(bvars[u1], GRB_EQUAL, bvars[u2]);
	}

	// guarantee a non-trivial partition
	GRBLinExpr exp1 = 0, exp2 = 0, exp3 = 0, exp4 = 0;
	for(int i = 0; i < gr.in_degree(root); i++)
	{
		exp1 += bvars[i];
		exp2 += (1 - bvars[i]);
	}
	for(int i = gr.in_degree(root); i < gr.degree(root); i++)
	{
		exp3 += bvars[i];
		exp4 += (1 - bvars[i]);
	}
	model->addConstr(exp1, GRB_GREATER_EQUAL, 1);
	model->addConstr(exp2, GRB_GREATER_EQUAL, 1);
	model->addConstr(exp3, GRB_GREATER_EQUAL, 1);
	model->addConstr(exp4, GRB_GREATER_EQUAL, 1);

	// lower bounds and upper bounds
	vector<double> weights;
	for(int i = 0; i < u2e.size(); i++)
	{
		edge_descriptor e = i2e[u2e[i]];
		double w = gr.get_edge_weight(e);
		weights.push_back(w);
	}

	// balance
	GRBLinExpr pexp1 = 0, pexp2 = 0, qexp1 = 0, qexp2 = 0;
	for(int i = 0; i < gr.in_degree(root); i++)
	{
		pexp1 += bvars[i] * weights[i];
		qexp1 += (1 - bvars[i]) * weights[i];
	}
	for(int i = gr.in_degree(root); i < gr.degree(root); i++)
	{
		pexp2 += bvars[i] * weights[i];
		qexp2 += (1 - bvars[i]) * weights[i];
	}
	model->addConstr(pexp1, GRB_GREATER_EQUAL, pexp2 - xvar);
	model->addConstr(pexp1, GRB_LESS_EQUAL, pexp2 + xvar);
	model->addConstr(qexp1, GRB_GREATER_EQUAL, qexp2 - xvar);
	model->addConstr(qexp1, GRB_LESS_EQUAL, qexp2 + xvar);

	// set objective and optimize
	GRBLinExpr obj = xvar;
	model->setObjective(obj, GRB_MINIMIZE);
	model->getEnv().set(GRB_IntParam_OutputFlag, 0);
	model->update();

	model->optimize();

	int f = model->get(GRB_IntAttr_Status);

	if(f == GRB_OPTIMAL)
	{
		equation p, q;
		for(int i = 0; i < bvars.size(); i++)
		{
			int b = (bvars[i].get(GRB_DoubleAttr_X) >= 0.5) ? 1 : 0;
			if(b == 1 && i < gr.in_degree(root)) p.s.push_back(u2e[i]);
			if(b == 1 && i >=gr.in_degree(root)) p.t.push_back(u2e[i]);
			if(b == 0 && i < gr.in_degree(root)) q.s.push_back(u2e[i]);
			if(b == 0 && i >=gr.in_degree(root)) q.t.push_back(u2e[i]);
		}
		eqns.push_back(p);
		eqns.push_back(q);
	}

	delete model;
	return 0;
}

int router::evaluate()
{
	ratio = -1;
	for(int i = 0; i < eqns.size(); i++)
	{
		equation &p = eqns[i];
		double pw1 = 0, pw2 = 0;
		for(int i = 0; i < p.s.size(); i++)
		{
			double w = gr.get_edge_weight(i2e[p.s[i]]);
			pw1 += w;
		}
		for(int i = 0; i < p.t.size(); i++) 
		{
			double w = gr.get_edge_weight(i2e[p.t[i]]);
			pw2 += w;
		}

		assert(pw1 >= SMIN && pw2 >= SMIN);
		p.e = (pw1 > pw2) ? (pw1 / pw2) : (pw2 / pw1);

		if(ratio < p.e) ratio = p.e;
	}
	return 0;
}

int router::build_indices()
{
	e2u.clear();
	u2e.clear();

	edge_iterator it1, it2;
	for(tie(it1, it2) = gr.in_edges(root); it1 != it2; it1++)
	{
		int e = e2i[*it1];
		e2u.insert(PI(e, e2u.size()));
		u2e.push_back(e);
	}
	for(tie(it1, it2) = gr.out_edges(root); it1 != it2; it1++)
	{
		int e = e2i[*it1];
		e2u.insert(PI(e, e2u.size()));
		u2e.push_back(e);
	}

	return 0;
}

int router::add_route(const PI &p, double c)
{
	for(int i = 0; i < routes.size(); i++)
	{
		if(routes[i] == p)
		{
			counts[i] += c;
			return 0;
		}
	}

	routes.push_back(p);
	counts.push_back(c);
	return 0;
}

int router::remove_route(const PI &p)
{
	for(int i = 0; i < routes.size(); i++)
	{
		if(routes[i] == p)
		{
			routes.erase(routes.begin() + i);
			counts.erase(counts.begin() + i);
			return 0;
		}
	}
	return 0;
}

int router::update_routes()
{
	vector<PI> vv;
	vector<double> cc;

	for(int i = 0; i < routes.size(); i++)
	{
		PI &p = routes[i];
		double c = counts[i];
		edge_descriptor e1 = i2e[p.first];
		edge_descriptor e2 = i2e[p.second];
		assert(e1 != null_edge);
		assert(e2 != null_edge);
		if(e1->target() != root) continue;
		if(e2->source() != root) continue;
		vv.push_back(p);
		cc.push_back(c);
	}

	routes = vv;
	counts = cc;
	
	return 0;
}

int router::replace_in_edge(int ex, int ey)
{
	for(int i = 0; i < routes.size(); i++)
	{
		if(routes[i].first == ex) routes[i].first = ey;
	}
	return 0;
}

int router::replace_out_edge(int ex, int ey)
{
	for(int i = 0; i < routes.size(); i++)
	{
		if(routes[i].second == ex) routes[i].second = ey;
	}
	return 0;
}

int router::split_in_edge(int ex, int ey, double r)
{
	assert(r >= 0 && r <= 1.0);
	if(ex == ey) return 0;
	int n = routes.size();
	for(int i = 0; i < n; i++)
	{
		if(routes[i].first == ex)
		{
			add_route(PI(ey, routes[i].second), (1.0 - r) * counts[i]);
			counts[i] *= r;
		}
	}
	return 0;
}

int router::split_out_edge(int ex, int ey, double r)
{
	assert(r >= 0 && r <= 1.0);
	if(ex == ey) return 0;
	int n = routes.size();
	for(int i = 0; i < n; i++)
	{
		if(routes[i].second == ex)
		{
			add_route(PI(routes[i].first, ey), (1.0 - r) * counts[i]);
			counts[i] *= r;
		}
	}
	return 0;
}

int router::remove_in_edges(const vector<int> &v)
{
	set<int> s(v.begin(), v.end());

	vector<PI> vv;
	vector<double> cc;
	for(int i = 0; i < routes.size(); i++)
	{
		int x = routes[i].first;
		if(s.find(x) != s.end()) continue;
		vv.push_back(routes[i]);
		cc.push_back(counts[i]);
	}

	routes = vv;
	counts = cc;

	return 0;
}

int router::remove_out_edges(const vector<int> &v)
{
	set<int> s(v.begin(), v.end());

	vector<PI> vv;
	vector<double> cc;
	for(int i = 0; i < routes.size(); i++)
	{
		int x = routes[i].second;
		if(s.find(x) != s.end()) continue;
		vv.push_back(routes[i]);
		cc.push_back(counts[i]);
	}

	routes = vv;
	counts = cc;

	return 0;
}

int router::remove_in_edge(int x)
{
	vector<int> v;
	v.push_back(x);
	return remove_in_edges(v);
}

int router::remove_out_edge(int x)
{
	vector<int> v;
	v.push_back(x);
	return remove_out_edges(v);
}

double router::total_counts() const
{
	double s = 0;
	for(int i = 0; i < counts.size(); i++)
	{
		s += counts[i];
	}
	return s;
}

int router::print() const
{
	printf("router %d, #routes = %lu, total-counts = %.1lf, ratio = %.2lf\n", root, routes.size(), total_counts(), ratio);
	printf("in-edges = ( ");
	for(int i = 0; i < gr.in_degree(root); i++) printf("%d ", u2e[i]);
	printf("), out-edges = ( ");
	for(int i = gr.in_degree(root); i < gr.degree(root); i++) printf("%d ", u2e[i]);
	printf(")\n");

	for(int i = 0; i < eqns.size(); i++) eqns[i].print(i);
	printf("\n");

	/*
	for(int i = 0; i < routes.size(); i++)
	{
		printf("route %d (%d, %d), count = %.1lf\n", i, routes[i].first, routes[i].second, counts[i]);
	}
	*/
	return 0;
}
