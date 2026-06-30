#![allow(dead_code)]
#![allow(clippy::enum_variant_names)]

use std::cell::RefCell;
use std::rc::Rc;

use crate::cubical::interval::{DNF, I, dnf_bot, dnf_top, eval_interval};
use crate::cubical::syntax::{ElimCase, Level, Name, Term, beta, equiv_dom, is_bot_dnf, is_top_dnf, max_var, shift, subst};

pub type Env = Vec<Value>;

/// A shared reference to the global definition values.
/// All closures created during evaluation share the same `Globals` so that
/// recursive self-references resolve correctly after placeholder replacement.
pub type Globals = Rc<RefCell<Vec<Value>>>;

#[derive(Debug, Clone)]
pub enum Value {
    VNeutral(Neutral),
    VLam(Name, Closure),
    VApp(Box<Value>, Box<Value>),
    VPi(Name, Box<Value>, Closure),
    VSigma(Name, Box<Value>, Closure),
    VPair(Box<Value>, Box<Value>),
    VPath(Box<Value>, Box<Value>, Box<Value>),
    VPLam(Name, IClosure),
    VPApp(Box<Value>, Box<Value>),
    VUniv(Level),
    VIntervalTy,
    VInterval(I),
    VIntervalVar(usize),
    VCube(DNF),
    VData(Name),
    VCon(Name, Name, Vec<Value>),
    VPCon(Name, Name, Vec<Value>, Box<Value>),
    VElim(Box<Value>, Vec<ElimCase>, Box<Value>),
    VGlue(Box<Value>, DNF, Box<Value>),
    VGlueElem(DNF, Box<Value>, Box<Value>),
    VUnglue(DNF, Box<Value>, Box<Value>),
    VEquiv(Box<Value>, Box<Value>),
    VMkEquiv(
        Box<Value>,
        Box<Value>,
        Box<Value>,
        Box<Value>,
        Box<Value>,
        Box<Value>,
    ),
    VEquivFwd(Box<Value>, Box<Value>),
    VUa(Box<Value>),
    VTransport(Box<Value>, Box<Value>),
    VHComp(Box<Value>, DNF, Box<Value>, Box<Value>),
    VFst(Box<Value>),
    VSnd(Box<Value>),
}

#[derive(Debug, Clone)]
pub struct Closure {
    pub env: Env,
    pub globals: Globals,
    pub global_offset: usize,
    pub body: Term,
}

#[derive(Debug, Clone)]
pub struct IClosure {
    pub env: Env,
    pub globals: Globals,
    pub global_offset: usize,
    pub body: Term,
}

#[derive(Debug, Clone)]
pub enum Neutral {
    NVar(usize),
    NApp(Box<Neutral>, Box<Value>),
    NPApp(Box<Neutral>, Box<Value>),
    NFst(Box<Neutral>),
    NSnd(Box<Neutral>),
    NElim(Box<Value>, Vec<ElimCase>, Box<Neutral>),
    NTransport(Box<Value>, Box<Value>),
    NHComp(Box<Value>, DNF, Box<Value>, Box<Value>),
}

impl Closure {
    pub fn apply(&self, v: Value) -> Value {
        let mut env = vec![v];
        env.extend_from_slice(&self.env);
        eval_nbe(&env, &self.globals, self.global_offset, &self.body)
    }
}

impl IClosure {
    pub fn apply_i(&self, i: I) -> Value {
        self.apply_interval_value(Value::VInterval(i))
    }

    fn apply_i_var(&self, level: usize) -> Value {
        self.apply_interval_value(Value::VIntervalVar(level))
    }

    fn apply_interval_value(&self, v: Value) -> Value {
        let mut env = vec![v];
        env.extend_from_slice(&self.env);
        eval_nbe(&env, &self.globals, self.global_offset, &self.body)
    }
}

/// Evaluate a term with local variables in `env` and global definitions in `globals`.
///
/// `global_offset` is the index into `globals` (in env.defs order, most-recent-first)
/// corresponding to the definition whose body is being evaluated.
/// A TVar(k) where k >= env.len() is a global reference:
///   globals[global_offset + (k - env.len())]
/// UNLESS that is also out of bounds — in which case we create a neutral.
pub fn eval_nbe(env: &[Value], globals: &Globals, global_offset: usize, t: &Term) -> Value {
    match t {
        Term::TVar(i) => {
            let i = *i as usize;
            if i < env.len() {
                env[i].clone()
            } else {
                let g = globals.borrow();
                let global_idx = global_offset + (i - env.len());
                if global_idx < g.len() {
                    g[global_idx].clone()
                } else {
                    Value::VNeutral(Neutral::NVar(global_idx - g.len()))
                }
            }
        }
        Term::TApp(f, a) => do_apply(
            eval_nbe(env, globals, global_offset, f),
            eval_nbe(env, globals, global_offset, a),
        ),
        Term::TAbs(x, b) => Value::VLam(
            x.clone(),
            Closure {
                env: env.to_vec(),
                globals: globals.clone(),
                global_offset,
                body: (**b).clone(),
            },
        ),
        Term::TUniv(n) => Value::VUniv(*n),
        Term::TIntervalTy => Value::VIntervalTy,
        Term::TPi(x, a, b) => Value::VPi(
            x.clone(),
            Box::new(eval_nbe(env, globals, global_offset, a)),
            Closure {
                env: env.to_vec(),
                globals: globals.clone(),
                global_offset,
                body: (**b).clone(),
            },
        ),
        Term::TInterval(i) => Value::VInterval(i.clone()),
        Term::TCube(c) => Value::VCube(c.clone()),
        Term::TPath(a, u, v) => Value::VPath(
            Box::new(eval_nbe(env, globals, global_offset, a)),
            Box::new(eval_nbe(env, globals, global_offset, u)),
            Box::new(eval_nbe(env, globals, global_offset, v)),
        ),
        Term::PLam(x, b) => Value::VPLam(
            x.clone(),
            IClosure {
                env: env.to_vec(),
                globals: globals.clone(),
                global_offset,
                body: (**b).clone(),
            },
        ),
        Term::PApp(p, r) => do_papp(
            eval_nbe(env, globals, global_offset, p),
            eval_nbe(env, globals, global_offset, r),
        ),
        Term::THComp(a, phi, tube, base) => do_hcomp(
            eval_nbe(env, globals, global_offset, a),
            value_to_dnf(eval_nbe(env, globals, global_offset, phi)),
            eval_nbe(env, globals, global_offset, tube),
            eval_nbe(env, globals, global_offset, base),
        ),
        Term::TEquiv(a, b) => Value::VEquiv(
            Box::new(eval_nbe(env, globals, global_offset, a)),
            Box::new(eval_nbe(env, globals, global_offset, b)),
        ),
        Term::TMkEquiv(a, b, f, g, eta, eps) => Value::VMkEquiv(
            Box::new(eval_nbe(env, globals, global_offset, a)),
            Box::new(eval_nbe(env, globals, global_offset, b)),
            Box::new(eval_nbe(env, globals, global_offset, f)),
            Box::new(eval_nbe(env, globals, global_offset, g)),
            Box::new(eval_nbe(env, globals, global_offset, eta)),
            Box::new(eval_nbe(env, globals, global_offset, eps)),
        ),
        Term::TEquivFwd(e, x) => do_equiv_fwd(
            eval_nbe(env, globals, global_offset, e),
            eval_nbe(env, globals, global_offset, x),
        ),
        Term::TUa(e) => Value::VUa(Box::new(eval_nbe(env, globals, global_offset, e))),
        Term::TTransport(p, x) => {
            let p_val = eval_nbe(env, globals, global_offset, p);
            let x_val = eval_nbe(env, globals, global_offset, x);
            let res = do_transport(env, globals, global_offset, p_val.clone(), x_val.clone());
            match &res {
                Value::VTransport(_, _) | Value::VNeutral(Neutral::NTransport(_, _)) => {
                    let p_term = quote(env.len(), globals, global_offset, p_val);
                    let x_term = quote(env.len(), globals, global_offset, x_val);
                    let reduced = transport_term_fallback(p_term, x_term);
                    match reduced {
                        Term::TTransport(_, _) => res,
                        _ => eval_nbe(env, globals, global_offset, &reduced),
                    }
                }
                _ => res,
            }
        }
        Term::TGlue(a, phi, te) => {
            let phi = value_to_dnf(eval_nbe(env, globals, global_offset, phi));
            let te = eval_nbe(env, globals, global_offset, te);
            if phi == dnf_top() {
                match te {
                    Value::VLam(_, clos) => {
                        let body = clos.apply(Value::VInterval(I::I1));
                        equiv_dom_value(body)
                    }
                    other => equiv_dom_value(other),
                }
            } else if phi == dnf_bot() {
                eval_nbe(env, globals, global_offset, a)
            } else {
                Value::VGlue(Box::new(eval_nbe(env, globals, global_offset, a)), phi, Box::new(te))
            }
        }
        Term::TGlueElem(phi, t, a) => {
            let phi = value_to_dnf(eval_nbe(env, globals, global_offset, phi));
            if phi == dnf_top() {
                eval_nbe(env, globals, global_offset, t)
            } else if phi == dnf_bot() {
                eval_nbe(env, globals, global_offset, a)
            } else {
                Value::VGlueElem(phi, Box::new(eval_nbe(env, globals, global_offset, t)), Box::new(eval_nbe(env, globals, global_offset, a)))
            }
        }
        Term::TUnglue(phi, te, g) => {
            let phi = value_to_dnf(eval_nbe(env, globals, global_offset, phi));
            let te = eval_nbe(env, globals, global_offset, te);
            let g_val = eval_nbe(env, globals, global_offset, g);
            if phi == dnf_top() {
                do_equiv_fwd(te, g_val)
            } else if phi == dnf_bot() {
                g_val
            } else {
                match &g_val {
                    Value::VGlueElem(g_phi, _, a) if *g_phi == phi => *a.clone(),
                    _ => Value::VUnglue(phi, Box::new(te), Box::new(g_val)),
                }
            }
        }
        Term::TSigma(x, a, b) => Value::VSigma(
            x.clone(),
            Box::new(eval_nbe(env, globals, global_offset, a)),
            Closure {
                env: env.to_vec(),
                globals: globals.clone(),
                global_offset,
                body: (**b).clone(),
            },
        ),
        Term::TPair(a, b) => Value::VPair(
            Box::new(eval_nbe(env, globals, global_offset, a)),
            Box::new(eval_nbe(env, globals, global_offset, b)),
        ),
        Term::TFst(p) => do_fst(eval_nbe(env, globals, global_offset, p)),
        Term::TSnd(p) => do_snd(eval_nbe(env, globals, global_offset, p)),
        Term::TData(d) => Value::VData(d.clone()),
        Term::TCon(data, con, args) => Value::VCon(
            data.clone(),
            con.clone(),
            args.iter().map(|a| eval_nbe(env, globals, global_offset, a)).collect(),
        ),
        Term::TPCon(data, con, args, r) => Value::VPCon(
            data.clone(),
            con.clone(),
            args.iter().map(|a| eval_nbe(env, globals, global_offset, a)).collect(),
            Box::new(eval_nbe(env, globals, global_offset, r)),
        ),
        Term::TElim(motive, cases, scrut) => {
            do_elim(
                eval_nbe(env, globals, global_offset, motive),
                cases,
                eval_nbe(env, globals, global_offset, scrut),
                env,
                globals,
                global_offset,
            )
        }
    }
}

pub fn do_apply(f: Value, a: Value) -> Value {
    match f {
        Value::VLam(_, clos) => clos.apply(a),
        Value::VNeutral(n) => Value::VNeutral(Neutral::NApp(Box::new(n), Box::new(a))),
        other => Value::VApp(Box::new(other), Box::new(a)),
    }
}

pub fn do_papp(p: Value, r: Value) -> Value {
    if let Some(i) = value_to_endpoint(&r)
        && let Value::VPLam(_, clos) = p {
            return clos.apply_i(i);
        }

    match p {
        Value::VPLam(_, clos) => match r {
            Value::VInterval(i) => clos.apply_i(i),
            Value::VIntervalVar(level) => clos.apply_i_var(level),
            other => Value::VPApp(
                Box::new(Value::VPLam("_".to_string(), clos)),
                Box::new(other),
            ),
        },
        Value::VNeutral(n) => Value::VNeutral(Neutral::NPApp(Box::new(n), Box::new(r))),
        other => Value::VPApp(Box::new(other), Box::new(r)),
    }
}

pub fn do_fst(p: Value) -> Value {
    match p {
        Value::VPair(a, _) => *a,
        Value::VNeutral(n) => Value::VNeutral(Neutral::NFst(Box::new(n))),
        other => Value::VFst(Box::new(other)),
    }
}

pub fn do_snd(p: Value) -> Value {
    match p {
        Value::VPair(_, b) => *b,
        Value::VNeutral(n) => Value::VNeutral(Neutral::NSnd(Box::new(n))),
        other => Value::VSnd(Box::new(other)),
    }
}

pub fn do_elim(motive: Value, cases: &[ElimCase], scrut: Value, env: &[Value], globals: &Globals, global_offset: usize) -> Value {
    match scrut {
        Value::VCon(_, con, args) => match cases.iter().find(|case| case.con == con) {
            Some(case) => {
                let mut env2: Env = args.into_iter().rev().collect();
                env2.extend_from_slice(env);
                eval_nbe(&env2, globals, global_offset, &case.body)
            }
            None => Value::VElim(
                Box::new(motive),
                cases.to_vec(),
                Box::new(Value::VCon("".into(), con, args)),
            ),
        },
        Value::VPCon(_, con, args, r) => match cases.iter().find(|case| case.con == con) {
            Some(case) => {
                let mut env2: Env = args.into_iter().rev().collect();
                env2.extend_from_slice(env);
                let body = eval_nbe(&env2, globals, global_offset, &case.body);
                do_papp(body, *r)
            }
            None => Value::VElim(
                Box::new(motive),
                cases.to_vec(),
                Box::new(Value::VPCon("".into(), con, args, r)),
            ),
        },
        Value::VNeutral(n) => stuck_elim(motive, cases, n),
        other => Value::VElim(Box::new(motive), cases.to_vec(), Box::new(other)),
    }
}

pub fn do_transport(env: &[Value], globals: &Globals, global_offset: usize, p: Value, x: Value) -> Value {
    match p {
        Value::VUa(e) => do_equiv_fwd(*e, x),
        Value::VPLam(ref i_name, ref clos) => {
            let b0 = clos.apply_i(I::I0);
            let b1 = clos.apply_i(I::I1);
            if quote(0, globals, global_offset, b0.clone()) == quote(0, globals, global_offset, b1.clone()) {
                return x;
            }


            match (&b0, &b1) {
                (Value::VUniv(_), Value::VUniv(_)) => x,

                // Pi transport (non-dependent codomain only)
                (Value::VPi(arg_name, _, _), Value::VPi(_, _, _)) => {
                    transport_pi(env, globals, global_offset, i_name, clos, arg_name, x)
                }

                // Path transport
                (Value::VPath(_, _, _), Value::VPath(_, _, _)) => {
                    transport_path(env, globals, global_offset, i_name, clos, x)
                }

                // Sigma transport (pair only)
                (Value::VSigma(_, _, _), Value::VSigma(_, _, _)) => {
                    match x {
                        Value::VPair(ref a, ref b) => {
                            transport_sigma_pair(env, globals, global_offset, i_name, clos, a, b)
                        }
                        _ => Value::VTransport(Box::new(Value::VPLam("_".to_string(), clos.clone())), Box::new(x)),
                    }
                }

                // Glue transport (phi=bot or phi=top)
                (Value::VGlue(_, phi0, _), Value::VGlue(_, _, _)) => {
                    let r = transport_glue(env, globals, global_offset, i_name, clos, phi0, &x);
                    r.unwrap_or_else(|| {
                        Value::VTransport(Box::new(Value::VPLam("_".to_string(), clos.clone())), Box::new(x))
                    })
                }

                _ => Value::VTransport(Box::new(Value::VPLam("_".to_string(), clos.clone())), Box::new(x)),
            }
        }
        other => Value::VNeutral(Neutral::NTransport(Box::new(other), Box::new(x))),
    }
}

/// Evaluate the body of a PLam at a formal interval variable (TVar(0) in the
/// returned term will be the interval binder).
fn eval_body_at_formal_interval(env: &[Value], globals: &Globals, global_offset: usize, clos: &IClosure) -> (Vec<Value>, Value) {
    let body_with_var = beta(
        &shift(1, 0, &clos.body),
        &Term::TVar(0),
    );
    let mut formal_env = vec![Value::VIntervalVar(env.len())];
    formal_env.extend_from_slice(env);
    let evaluated = eval_nbe(&formal_env, globals, global_offset, &body_with_var);
    (formal_env, evaluated)
}

/// Apply a Closure with a dummy argument (for non-dependent extraction).
fn apply_non_dep(clos: &Closure) -> Value {
    clos.apply(Value::VInterval(I::I0))
}

/// Check whether a term references the first de Bruijn variable (index 0).
fn uses_tvar_0(t: &Term) -> bool {
    match t {
        Term::TVar(i) => *i == 0,
        Term::TApp(f, a) => uses_tvar_0(f) || uses_tvar_0(a),
        Term::TAbs(_, b) => uses_tvar_0(b),
        Term::TPi(_, a, b) => uses_tvar_0(a) || uses_tvar_0(b),
        Term::TPath(a, u, v) => uses_tvar_0(a) || uses_tvar_0(u) || uses_tvar_0(v),
        Term::PLam(_, b) => uses_tvar_0(b),
        Term::PApp(p, r) => uses_tvar_0(p) || uses_tvar_0(r),
        Term::THComp(a, phi, u, u0) => uses_tvar_0(a) || uses_tvar_0(phi) || uses_tvar_0(u) || uses_tvar_0(u0),
        Term::TEquiv(a, b) => uses_tvar_0(a) || uses_tvar_0(b),
        Term::TMkEquiv(a, b, f, g, eta, eps) => {
            uses_tvar_0(a) || uses_tvar_0(b) || uses_tvar_0(f) || uses_tvar_0(g) || uses_tvar_0(eta) || uses_tvar_0(eps)
        }
        Term::TEquivFwd(e, x) => uses_tvar_0(e) || uses_tvar_0(x),
        Term::TUa(e) => uses_tvar_0(e),
        Term::TTransport(p, x) => uses_tvar_0(p) || uses_tvar_0(x),
        Term::TGlue(a, phi, te) => uses_tvar_0(a) || uses_tvar_0(phi) || uses_tvar_0(te),
        Term::TGlueElem(phi, t, a) => uses_tvar_0(phi) || uses_tvar_0(t) || uses_tvar_0(a),
        Term::TUnglue(phi, te, g) => uses_tvar_0(phi) || uses_tvar_0(te) || uses_tvar_0(g),
        Term::TSigma(_, a, b) => uses_tvar_0(a) || uses_tvar_0(b),
        Term::TPair(a, b) => uses_tvar_0(a) || uses_tvar_0(b),
        Term::TFst(p) => uses_tvar_0(p),
        Term::TSnd(p) => uses_tvar_0(p),
        Term::TUniv(_) | Term::TIntervalTy | Term::TInterval(_) | Term::TCube(_) | Term::TData(_) => false,
        Term::TCon(_, _, args) => args.iter().any(uses_tvar_0),
        Term::TPCon(_, _, args, r) => args.iter().any(uses_tvar_0) || uses_tvar_0(r),
        Term::TElim(motive, cases, scrut) => {
            uses_tvar_0(motive) || uses_tvar_0(scrut) || cases.iter().any(|c| uses_tvar_0(&c.body))
        }
    }
}

/// Transport through Pi types.
fn transport_pi(env: &[Value], globals: &Globals, global_offset: usize, i_name: &str, clos: &IClosure, arg_name: &str, x: Value) -> Value {
    let (formal_env, pi_at_var) = eval_body_at_formal_interval(env, globals, global_offset, clos);
    let cod_clos = match &pi_at_var {
        Value::VPi(_, _, cod_clos) => cod_clos,
        _ => return Value::VTransport(
            Box::new(Value::VPLam("_".to_string(), clos.clone())),
            Box::new(x),
        ),
    };

    if !uses_tvar_0(&cod_clos.body) {
        let b_val = apply_non_dep(cod_clos);
        let b_body = shift(1, 1, &quote(formal_env.len(), globals, global_offset, b_val));
        let b_fam = Term::PLam(i_name.to_string(), Box::new(b_body));
        let x_term = quote(env.len(), globals, global_offset, x);
        let result = Term::TAbs(
            arg_name.to_string(),
            Box::new(Term::TTransport(
                Box::new(b_fam),
                Box::new(Term::TApp(
                    Box::new(shift(1, 0, &x_term)),
                    Box::new(Term::TVar(0)),
                )),
            )),
        );
        eval_nbe(env, globals, global_offset, &result)
    } else {
        let p_term = quote(env.len(), globals, global_offset, Value::VPLam(i_name.to_string(), clos.clone()));
        let x_term = quote(env.len(), globals, global_offset, x.clone());
        let reduced = transport_term_fallback(p_term, x_term);
        match reduced {
            Term::TTransport(_, _) => Value::VTransport(
                Box::new(Value::VPLam("_".to_string(), clos.clone())),
                Box::new(x),
            ),
            _ => eval_nbe(env, globals, global_offset, &reduced),
        }
    }
}

/// Transport through Path types.
fn transport_path(env: &[Value], globals: &Globals, global_offset: usize, i_name: &str, clos: &IClosure, x: Value) -> Value {
    let (formal_env, path_at_var) = eval_body_at_formal_interval(env, globals, global_offset, clos);
    let a_val = match &path_at_var {
        Value::VPath(a, _, _) => *a.clone(),
        _ => return Value::VTransport(
            Box::new(Value::VPLam("_".to_string(), clos.clone())),
            Box::new(x),
        ),
    };
    let a_body = shift(1, 1, &quote(formal_env.len(), globals, global_offset, a_val));
    let a_fam = Term::PLam(i_name.to_string(), Box::new(a_body));
    let x_term = quote(env.len(), globals, global_offset, x);
    let a_fam_s = shift(1, 0, &a_fam);
    let result = Term::PLam(
        "j".to_string(),
        Box::new(Term::TTransport(
            Box::new(a_fam_s),
            Box::new(Term::PApp(
                Box::new(shift(1, 0, &x_term)),
                Box::new(Term::TVar(0)),
            )),
        )),
    );
    eval_nbe(env, globals, global_offset, &result)
}

/// Transport through Sigma types (pair decomposition).
fn transport_sigma_pair(
    env: &[Value],
    globals: &Globals,
    global_offset: usize,
    i_name: &str,
    clos: &IClosure,
    a: &Value,
    b: &Value,
) -> Value {
    let (formal_env, sigma_at_var) = eval_body_at_formal_interval(env, globals, global_offset, clos);
    let a_val = match &sigma_at_var {
        Value::VSigma(_, a_val, _) => *a_val.clone(),
        _ => Value::VUniv(0),
    };
    let a_body = shift(1, 1, &quote(formal_env.len(), globals, global_offset, a_val));
    let a_fam = Term::PLam(i_name.to_string(), Box::new(a_body));

    let a_prime = eval_nbe(env, globals, global_offset, &Term::TTransport(
        Box::new(a_fam.clone()),
        Box::new(quote(env.len(), globals, global_offset, a.clone())),
    ));

    let b_val = match &sigma_at_var {
        Value::VSigma(_, _, cod_clos) => apply_non_dep(cod_clos),
        _ => Value::VUniv(0),
    };
    let b_body = shift(1, 1, &quote(formal_env.len(), globals, global_offset, b_val));
    let b_fam = Term::PLam(i_name.to_string(), Box::new(b_body));

    let b_prime = eval_nbe(env, globals, global_offset, &Term::TTransport(
        Box::new(b_fam),
        Box::new(quote(env.len(), globals, global_offset, b.clone())),
    ));

    Value::VPair(Box::new(a_prime), Box::new(b_prime))
}

/// Transport through Glue types (phi=bot or phi=top).
fn transport_glue(
    env: &[Value],
    globals: &Globals,
    global_offset: usize,
    i_name: &str,
    clos: &IClosure,
    phi0: &DNF,
    x: &Value,
) -> Option<Value> {
    if *phi0 == dnf_bot() {
        let (formal_env, glue_at_var) = eval_body_at_formal_interval(env, globals, global_offset, clos);
        let a_val = match &glue_at_var {
            Value::VGlue(a, _, _) => *a.clone(),
            _ => return None,
        };
        let a_body = shift(1, 1, &quote(formal_env.len(), globals, global_offset, a_val));
        let a_fam = Term::PLam(i_name.to_string(), Box::new(a_body));
        Some(eval_nbe(env, globals, global_offset, &Term::TTransport(
            Box::new(a_fam),
            Box::new(quote(env.len(), globals, global_offset, x.clone())),
        )))
    } else if *phi0 == dnf_top() {
        let (formal_env, glue_at_var) = eval_body_at_formal_interval(env, globals, global_offset, clos);
        let te_val = match &glue_at_var {
            Value::VGlue(_, _, te) => *te.clone(),
            _ => return None,
        };
        let dom = equiv_dom_value(te_val);
        let dom_body = shift(1, 1, &quote(formal_env.len(), globals, global_offset, dom));
        let dom_fam = Term::PLam(i_name.to_string(), Box::new(dom_body));
        Some(eval_nbe(env, globals, global_offset, &Term::TTransport(
            Box::new(dom_fam),
            Box::new(quote(env.len(), globals, global_offset, x.clone())),
        )))
    } else {
        None
    }
}

/// Term-level transport reduction.
pub fn transport_term_fallback(p_: Term, x_: Term) -> Term {
    match p_ {
        Term::TUa(ref e) => nbe_eval(&Term::TEquivFwd(e.clone(), Box::new(x_))),

        Term::PLam(ref i_name, ref body) => {
            let b0 = nbe_eval(&beta(body, &Term::TInterval(I::I0)));
            let b1 = nbe_eval(&beta(body, &Term::TInterval(I::I1)));

            if b0 == b1 {
                return x_;
            }

            match (&b0, &b1) {
                (Term::TPi(arg_name, a0, _), Term::TPi(_, a1, _)) => {
                    let arg_name = arg_name.clone();
                    let i_name = i_name.clone();

                    let a0_eval = nbe_eval(a0);
                    let a1_eval = nbe_eval(a1);
                    if a0_eval == a1_eval {
                        let b_fam = Term::PLam(
                            i_name.clone(),
                            Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                                Term::TPi(_, _, b_i) => {
                                    let max_idx = max_var(&b_i);
                                    let temp = max_idx + 1;
                                    let tmp_var = Term::TVar(temp);
                                    let step1 = subst(0, &tmp_var, &b_i);
                                    let step2 = subst(1, &Term::TVar(0), &step1);
                                    subst(temp, &Term::TVar(1), &step2)
                                }
                                _ => {
                                    let b0_body = match &b0 {
                                        Term::TPi(_, _, b) => (**b).clone(),
                                        _ => b0.clone(),
                                    };
                                    shift(1, 0, &b0_body)
                                }
                            }),
                        );
                        let x_shifted = shift(1, 0, &x_);
                        Term::TAbs(
                            arg_name,
                            Box::new(nbe_eval(&Term::TTransport(
                                Box::new(b_fam),
                                Box::new(nbe_eval(&Term::TApp(Box::new(x_shifted), Box::new(Term::TVar(0))))),
                            ))),
                        )
                    } else {
                        let b_non_dep = match &b0 {
                            Term::TPi(_, _, b0_body) => subst(0, &Term::TUniv(0), b0_body) == **b0_body,
                            _ => false,
                        };
                        if b_non_dep {
                            let b0_body = match &b0 {
                                Term::TPi(_, _, b) => (**b).clone(),
                                _ => b0.clone(),
                            };
                            let b_fam = Term::PLam(
                                i_name.clone(),
                                Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                                    Term::TPi(_, _, b_i) => *b_i,
                                    _ => shift(1, 0, &b0_body),
                                }),
                            );
                            let x_shifted = shift(1, 0, &x_);
                            Term::TAbs(
                                arg_name,
                                Box::new(nbe_eval(&Term::TTransport(
                                    Box::new(b_fam),
                                    Box::new(nbe_eval(&Term::TApp(Box::new(x_shifted), Box::new(Term::TVar(0))))),
                                ))),
                            )
                        } else {
                            let arg_name = arg_name.clone();
                            let i_name = i_name.clone();

                            let pi_at_var = nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0)));
                            let a_i = match &pi_at_var {
                                Term::TPi(_, a, _) => (**a).clone(),
                                _ => shift(1, 0, a0),
                            };
                            let b0_body = match &b0 {
                                Term::TPi(_, _, b) => (**b).clone(),
                                _ => b0.clone(),
                            };
                            let b_i = match &pi_at_var {
                                Term::TPi(_, _, b) => (**b).clone(),
                                _ => shift(1, 0, &b0_body),
                            };

                            let a_fam = Term::PLam(i_name.clone(), Box::new(a_i));
                            let a_rev_fam = Term::PLam(
                                "j".to_string(),
                                Box::new(Term::PApp(
                                    Box::new(shift(1, 0, &a_fam)),
                                    Box::new(Term::TInterval(I::Neg(Box::new(I::Var(0))))),
                                )),
                            );

                            let y0_term = Term::TTransport(
                                Box::new(shift(1, 0, &a_rev_fam)),
                                Box::new(Term::TVar(0)),
                            );

                            let b_fam = Term::PLam(
                                i_name.clone(),
                                Box::new({
                                    let max_idx = max_var(&b_i);
                                    let temp = max_idx + 1;
                                    let tmp_var = Term::TVar(temp);
                                    let step1 = subst(0, &tmp_var, &b_i);
                                    let step2 = subst(1, &Term::TVar(0), &step1);
                                    let b_i_swapped = subst(temp, &Term::TVar(1), &step2);

                                    let y0_shifted = shift(1, 0, &y0_term);
                                    let fill_at_i = nbe_eval(&Term::TTransport(
                                        Box::new(Term::PLam(
                                            "j".to_string(),
                                            Box::new(nbe_eval(&Term::PApp(
                                                Box::new(shift(2, 0, &a_fam)),
                                                Box::new(Term::TInterval(I::Meet(
                                                    Box::new(I::Var(1)),
                                                    Box::new(I::Var(0)),
                                                ))),
                                            ))),
                                        )),
                                        Box::new(y0_shifted),
                                    ));
                                    nbe_eval(&subst(1, &fill_at_i, &b_i_swapped))
                                }),
                            );

                            let x_shifted = shift(1, 0, &x_);
                            Term::TAbs(
                                arg_name,
                                Box::new(nbe_eval(&Term::TTransport(
                                    Box::new(b_fam),
                                    Box::new(nbe_eval(&Term::TApp(
                                        Box::new(x_shifted),
                                        Box::new(y0_term),
                                    ))),
                                ))),
                            )
                        }
                    }
                }

                (Term::TPath(ty_a0, _, _), Term::TPath(_, _, _)) => {
                    let i_name = i_name.clone();
                    let ty_a0 = (**ty_a0).clone();

                    let a_fam = Term::PLam(
                        i_name.clone(),
                        Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                            Term::TPath(a, _, _) => *a,
                            _ => shift(1, 0, &ty_a0),
                        }),
                    );

                    let a_fam_s = shift(1, 0, &a_fam);
                    let x_shifted = shift(1, 0, &x_);
                    Term::PLam(
                        "j".to_string(),
                        Box::new(nbe_eval(&Term::TTransport(
                            Box::new(a_fam_s),
                            Box::new(Term::PApp(Box::new(x_shifted), Box::new(Term::TVar(0)))),
                        ))),
                    )
                }

                (Term::TSigma(_, _, _), Term::TSigma(_, _, _)) => {
                    match x_ {
                        Term::TPair(ref a, ref b) => {
                            let i_name = i_name.clone();

                            let b0_a = match &b0 {
                                Term::TSigma(_, a, _) => (**a).clone(),
                                _ => b0.clone(),
                            };
                            let b0_b = match &b0 {
                                Term::TSigma(_, _, bz) => (**bz).clone(),
                                _ => b0.clone(),
                            };

                            let a_fam = Term::PLam(
                                i_name.clone(),
                                Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                                    Term::TSigma(_, a_i, _) => *a_i,
                                    _ => shift(1, 0, &b0_a),
                                }),
                            );

                            let a_prime =
                                nbe_eval(&Term::TTransport(Box::new(a_fam.clone()), a.clone()));

                            let a_clone = (**a).clone();
                            let b_fam = Term::PLam(
                                i_name.clone(),
                                Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                                    Term::TSigma(_, _, b_i) => {
                                        let fill_at_i = nbe_eval(&Term::TTransport(
                                            Box::new(Term::PLam(
                                                "j".to_string(),
                                                Box::new(nbe_eval(&Term::PApp(
                                                    Box::new(shift(2, 0, &a_fam)),
                                                    Box::new(Term::TInterval(I::Meet(
                                                        Box::new(I::Var(1)),
                                                        Box::new(I::Var(0)),
                                                    ))),
                                                ))),
                                            )),
                                            Box::new(shift(1, 0, &a_clone)),
                                        ));
                                        nbe_eval(&beta(&b_i, &fill_at_i))
                                    }
                                    _ => shift(1, 0, &b0_b),
                                }),
                            );

                            let b_prime = nbe_eval(&Term::TTransport(Box::new(b_fam), b.clone()));
                            Term::TPair(Box::new(a_prime), Box::new(b_prime))
                        }
                        _ => Term::TTransport(
                            Box::new(Term::PLam(i_name.clone(), body.clone())),
                            Box::new(x_),
                        ),
                    }
                }

                (Term::TGlue(_, phi0, _), Term::TGlue(_, _, _)) => {
                    let i_name = i_name.clone();
                    if is_bot_dnf(&nbe_eval(phi0)) {
                        nbe_eval(&Term::TTransport(
                            Box::new(Term::PLam(
                                i_name.clone(),
                                Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                                    Term::TGlue(a, _, _) => *a,
                                    other => other,
                                }),
                            )),
                            Box::new(x_),
                        ))
                    } else if is_top_dnf(&nbe_eval(phi0)) {
                        nbe_eval(&Term::TTransport(
                            Box::new(Term::PLam(
                                i_name.clone(),
                                Box::new(match nbe_eval(&beta(&shift(1, 0, body), &Term::TVar(0))) {
                                    Term::TGlue(_, _, te) => equiv_dom(&nbe_eval(&te)),
                                    other => other,
                                }),
                            )),
                            Box::new(x_),
                        ))
                    } else {
                        Term::TTransport(Box::new(Term::PLam(i_name, body.clone())), Box::new(x_))
                    }
                }

                _ => Term::TTransport(
                    Box::new(Term::PLam(i_name.clone(), body.clone())),
                    Box::new(x_),
                ),
            }
        }

        p_ => Term::TTransport(Box::new(p_), Box::new(x_)),
    }
}

pub fn do_hcomp(a_ty: Value, phi: DNF, tube: Value, base: Value) -> Value {
    if phi == dnf_top() {
        do_papp(tube, Value::VInterval(I::I1))
    } else if phi == dnf_bot() {
        base
    } else {
        Value::VHComp(Box::new(a_ty), phi, Box::new(tube), Box::new(base))
    }
}

pub fn quote(size: usize, globals: &Globals, global_offset: usize, v: Value) -> Term {
    match v {
        Value::VNeutral(n) => quote_neutral(size, globals, global_offset, n),
        Value::VLam(x, clos) => Term::TAbs(
            x,
            Box::new(quote(
                size + 1,
                globals,
                global_offset,
                clos.apply(Value::VNeutral(Neutral::NVar(size))),
            )),
        ),
        Value::VApp(f, a) => Term::TApp(Box::new(quote(size, globals, global_offset, *f)), Box::new(quote(size, globals, global_offset, *a))),
        Value::VPi(x, a, b) => Term::TPi(
            x,
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(quote(
                size + 1,
                globals,
                global_offset,
                b.apply(Value::VNeutral(Neutral::NVar(size))),
            )),
        ),
        Value::VSigma(x, a, b) => Term::TSigma(
            x,
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(quote(
                size + 1,
                globals,
                global_offset,
                b.apply(Value::VNeutral(Neutral::NVar(size))),
            )),
        ),
        Value::VPair(a, b) => Term::TPair(Box::new(quote(size, globals, global_offset, *a)), Box::new(quote(size, globals, global_offset, *b))),
        Value::VFst(p) => Term::TFst(Box::new(quote(size, globals, global_offset, *p))),
        Value::VSnd(p) => Term::TSnd(Box::new(quote(size, globals, global_offset, *p))),
        Value::VPath(a, u, v) => Term::TPath(
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(quote(size, globals, global_offset, *u)),
            Box::new(quote(size, globals, global_offset, *v)),
        ),
        Value::VPLam(x, clos) => Term::PLam(x, Box::new(quote(size + 1, globals, global_offset, clos.apply_i_var(size)))),
        Value::VPApp(p, r) => Term::PApp(Box::new(quote(size, globals, global_offset, *p)), Box::new(quote(size, globals, global_offset, *r))),
        Value::VUniv(n) => Term::TUniv(n),
        Value::VIntervalTy => Term::TIntervalTy,
        Value::VInterval(i) => Term::TInterval(i),
        Value::VIntervalVar(level) => level_to_var(size, level),
        Value::VCube(c) => Term::TCube(c),
        Value::VData(d) => Term::TData(d),
        Value::VCon(d, c, args) => {
            Term::TCon(d, c, args.into_iter().map(|a| quote(size, globals, global_offset, a)).collect())
        }
        Value::VPCon(d, c, args, r) => Term::TPCon(
            d,
            c,
            args.into_iter().map(|a| quote(size, globals, global_offset, a)).collect(),
            Box::new(quote(size, globals, global_offset, *r)),
        ),
        Value::VElim(motive, cases, scrut) => Term::TElim(
            Box::new(quote(size, globals, global_offset, *motive)),
            quote_cases(size, globals, global_offset, cases),
            Box::new(quote(size, globals, global_offset, *scrut)),
        ),
        Value::VGlue(a, phi, te) => Term::TGlue(
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(Term::TCube(phi)),
            Box::new(quote(size, globals, global_offset, *te)),
        ),
        Value::VGlueElem(phi, t, a) => Term::TGlueElem(
            Box::new(Term::TCube(phi)),
            Box::new(quote(size, globals, global_offset, *t)),
            Box::new(quote(size, globals, global_offset, *a)),
        ),
        Value::VUnglue(phi, te, g) => Term::TUnglue(
            Box::new(Term::TCube(phi)),
            Box::new(quote(size, globals, global_offset, *te)),
            Box::new(quote(size, globals, global_offset, *g)),
        ),
        Value::VEquiv(a, b) => Term::TEquiv(Box::new(quote(size, globals, global_offset, *a)), Box::new(quote(size, globals, global_offset, *b))),
        Value::VMkEquiv(a, b, f, g, eta, eps) => Term::TMkEquiv(
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(quote(size, globals, global_offset, *b)),
            Box::new(quote(size, globals, global_offset, *f)),
            Box::new(quote(size, globals, global_offset, *g)),
            Box::new(quote(size, globals, global_offset, *eta)),
            Box::new(quote(size, globals, global_offset, *eps)),
        ),
        Value::VEquivFwd(e, x) => {
            Term::TEquivFwd(Box::new(quote(size, globals, global_offset, *e)), Box::new(quote(size, globals, global_offset, *x)))
        }
        Value::VUa(e) => Term::TUa(Box::new(quote(size, globals, global_offset, *e))),
        Value::VTransport(p, x) => {
            Term::TTransport(Box::new(quote(size, globals, global_offset, *p)), Box::new(quote(size, globals, global_offset, *x)))
        }
        Value::VHComp(a, phi, tube, base) => Term::THComp(
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(Term::TCube(phi)),
            Box::new(quote(size, globals, global_offset, *tube)),
            Box::new(quote(size, globals, global_offset, *base)),
        ),
    }
}

fn quote_neutral(size: usize, globals: &Globals, global_offset: usize, n: Neutral) -> Term {
    match n {
        Neutral::NVar(level) => level_to_var(size, level),
        Neutral::NApp(f, a) => {
            Term::TApp(Box::new(quote_neutral(size, globals, global_offset, *f)), Box::new(quote(size, globals, global_offset, *a)))
        }
        Neutral::NPApp(p, r) => {
            Term::PApp(Box::new(quote_neutral(size, globals, global_offset, *p)), Box::new(quote(size, globals, global_offset, *r)))
        }
        Neutral::NFst(p) => Term::TFst(Box::new(quote_neutral(size, globals, global_offset, *p))),
        Neutral::NSnd(p) => Term::TSnd(Box::new(quote_neutral(size, globals, global_offset, *p))),
        Neutral::NElim(motive, cases, scrut) => Term::TElim(
            Box::new(quote(size, globals, global_offset, *motive)),
            quote_cases(size, globals, global_offset, cases),
            Box::new(quote_neutral(size, globals, global_offset, *scrut)),
        ),
        Neutral::NTransport(p, x) => {
            Term::TTransport(Box::new(quote(size, globals, global_offset, *p)), Box::new(quote(size, globals, global_offset, *x)))
        }
        Neutral::NHComp(a, phi, tube, base) => Term::THComp(
            Box::new(quote(size, globals, global_offset, *a)),
            Box::new(Term::TCube(phi)),
            Box::new(quote(size, globals, global_offset, *tube)),
            Box::new(quote(size, globals, global_offset, *base)),
        ),
    }
}

fn quote_cases(size: usize, globals: &Globals, global_offset: usize, cases: Vec<ElimCase>) -> Vec<ElimCase> {
    cases
        .into_iter()
        .map(|case| ElimCase {
            con: case.con,
            binders: case.binders.clone(),
            body: Box::new(normalize_under_binders(
                size,
                case.binders.len(),
                globals,
                global_offset,
                &case.body,
            )),
        })
        .collect()
}

fn normalize_under_binders(size: usize, binders: usize, globals: &Globals, global_offset: usize, body: &Term) -> Term {
    let mut env = Vec::new();
    for level in (size..size + binders).rev() {
        env.push(Value::VNeutral(Neutral::NVar(level)));
    }
    quote(size + binders, globals, global_offset, eval_nbe(&env, globals, global_offset, body))
}

pub fn normalize(env: &[Value], globals: &Globals, global_offset: usize, t: &Term) -> Term {
    quote(env.len(), globals, global_offset, eval_nbe(env, globals, global_offset, t))
}

/// Evaluate a closed term without global definitions (original behavior).
pub fn nbe_eval(t: &Term) -> Term {
    let empty_globals: Globals = Rc::new(RefCell::new(Vec::new()));
    let mv = max_var(t);
    if mv < 0 {
        normalize(&[], &empty_globals, 0, t)
    } else {
        let size = (mv + 1) as usize;
        let mut env = Vec::with_capacity(size);
        for level in (0..size).rev() {
            env.push(Value::VNeutral(Neutral::NVar(level)));
        }
        normalize(&env, &empty_globals, 0, t)
    }
}

/// Evaluate a term with access to global definition values.
///
/// `globals` should be ordered most-recent-first (same as `env.defs`).
/// `global_offset` is the index into `globals` where the evaluated term's
/// own definition lives (0 = most recent, the typical case for evaluating
/// the target expression).
pub fn nbe_eval_with_globals(t: &Term, globals: &Globals, global_offset: usize) -> Term {
    // The env starts empty — all TVars resolve to globals.
    // Lambdas push binders onto the env during evaluation via do_apply.
    normalize(&[], globals, global_offset, t)
}

fn do_equiv_fwd(e: Value, x: Value) -> Value {
    match e {
        Value::VMkEquiv(_, _, f, _, _, _) => do_apply(*f, x),
        other => Value::VEquivFwd(Box::new(other), Box::new(x)),
    }
}

fn equiv_dom_value(v: Value) -> Value {
    match v {
        Value::VMkEquiv(a, _, _, _, _, _) | Value::VEquiv(a, _) => *a,
        Value::VPair(a, _) => *a,
        other => other,
    }
}

fn stuck_elim(motive: Value, cases: &[ElimCase], n: Neutral) -> Value {
    Value::VNeutral(Neutral::NElim(
        Box::new(motive),
        cases.to_vec(),
        Box::new(n),
    ))
}

fn value_to_dnf(v: Value) -> DNF {
    match v {
        Value::VCube(d) => d,
        Value::VInterval(i) => eval_interval(&i),
        Value::VIntervalVar(level) => eval_interval(&I::Var(level as i32)),
        other => match quote(0, &Rc::new(RefCell::new(Vec::new())), 0, other) {
            Term::TCube(d) => d,
            Term::TInterval(i) => eval_interval(&i),
            _ => dnf_bot(),
        },
    }
}

fn value_to_endpoint(v: &Value) -> Option<I> {
    match v {
        Value::VInterval(i) => {
            let d = eval_interval(i);
            if d == dnf_bot() {
                Some(I::I0)
            } else if d == dnf_top() {
                Some(I::I1)
            } else {
                None
            }
        }
        Value::VCube(d) if *d == dnf_bot() => Some(I::I0),
        Value::VCube(d) if *d == dnf_top() => Some(I::I1),
        _ => None,
    }
}

fn level_to_var(size: usize, level: usize) -> Term {
    if level < size {
        Term::TVar((size - level - 1) as i32)
    } else {
        Term::TVar(level.saturating_sub(size) as i32)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn b(t: Term) -> Box<Term> {
        Box::new(t)
    }

    #[test]
    fn identity_function_normalizes_to_itself() {
        let id = Term::TAbs("x".to_string(), b(Term::TVar(0)));
        assert_eq!(nbe_eval(&id), id);
    }

    #[test]
    fn beta_reduces_identity_application() {
        let term = Term::TApp(
            b(Term::TAbs("x".to_string(), b(Term::TVar(0)))),
            b(Term::TUniv(0)),
        );
        assert_eq!(nbe_eval(&term), Term::TUniv(0));
    }

    #[test]
    fn fst_of_pair_reduces() {
        let term = Term::TFst(b(Term::TPair(b(Term::TUniv(0)), b(Term::TUniv(1)))));
        assert_eq!(nbe_eval(&term), Term::TUniv(0));
    }

    #[test]
    fn transport_over_constant_family_is_identity() {
        let family = Term::PLam("i".to_string(), b(Term::TUniv(0)));
        let term = Term::TTransport(b(family), b(Term::TUniv(1)));
        assert_eq!(nbe_eval(&term), Term::TUniv(1));
    }

    #[test]
    fn transport_over_nonconstant_pi_produces_lambda() {
        let body = Term::TPi(
            "x".to_string(),
            b(Term::TApp(b(Term::TVar(1)), b(Term::TVar(0)))),
            b(Term::TUniv(0)),
        );
        let fam = Term::PLam("i".to_string(), b(body));
        let arg = Term::TAbs("x".to_string(), b(Term::TUniv(0)));
        let term = Term::TTransport(b(fam), b(arg));
        let result = nbe_eval(&term);
        assert!(
            matches!(&result, Term::TAbs(_, _)),
            "expected TAbs, got: {}",
            crate::cubical::syntax::show_term(&[], &result)
        );
    }

    #[test]
    fn deep_transport_fallback_unsticks_pi() {
        let body = Term::TPi(
            "x".to_string(),
            b(Term::TApp(b(Term::TVar(1)), b(Term::TVar(0)))),
            b(Term::TUniv(0)),
        );
        let fam = Term::PLam("i".to_string(), b(body));
        let arg = Term::TAbs("x".to_string(), b(Term::TUniv(0)));
        let term = Term::TTransport(b(fam), b(arg));
        let result = nbe_eval(&term);
        assert!(
            !matches!(result, Term::TTransport(_, _)),
            "transport should not be stuck: {}",
            crate::cubical::syntax::show_term(&[], &result)
        );
    }

    #[test]
    fn sigma_transport_on_pair_reduces() {
        let sigma = Term::TSigma(
            "x".to_string(),
            b(Term::TUniv(0)),
            b(Term::TUniv(1)),
        );
        let fam = Term::PLam("i".to_string(), b(sigma));
        let pair = Term::TPair(b(Term::TUniv(0)), b(Term::TUniv(1)));
        let term = Term::TTransport(b(fam), b(pair.clone()));
        let result = nbe_eval(&term);
        assert_eq!(result, pair);
    }

    #[test]
    fn path_transport_produces_plam() {
        let path = Term::TPath(
            b(Term::TApp(b(Term::TVar(1)), b(Term::TVar(0)))),
            b(Term::TUniv(0)),
            b(Term::TUniv(0)),
        );
        let fam = Term::PLam("i".to_string(), b(path));
        let arg = Term::PLam("j".to_string(), b(Term::TUniv(0)));
        let term = Term::TTransport(b(fam), b(arg));
        let result = nbe_eval(&term);
        assert!(
            matches!(&result, Term::PLam(_, _)),
            "expected PLam, got: {}",
            crate::cubical::syntax::show_term(&[], &result)
        );
    }

    #[test]
    fn native_pi_transport_no_deep_fallback() {
        let body = Term::TPi(
            "x".to_string(),
            b(Term::TApp(b(Term::TVar(1)), b(Term::TVar(0)))),
            b(Term::TUniv(0)),
        );
        let fam = Term::PLam("i".to_string(), b(body));
        let arg = Term::TAbs("x".to_string(), b(Term::TUniv(0)));
        let term = Term::TTransport(b(fam), b(arg));
        let result = nbe_eval(&term);
        assert!(
            matches!(&result, Term::TAbs(_, _)),
            "expected TAbs (native Pi transport), got: {}",
            crate::cubical::syntax::show_term(&[], &result)
        );
    }
}
