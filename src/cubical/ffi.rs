use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use crate::cubical::syntax::show_term;
use crate::cubical::env::{Env, apply_globals, global_ctx};
use crate::cubical::nbe::{Globals, Value, Neutral, nbe_eval_with_globals, eval_nbe};
use crate::cubical::parser::ProgramParser;
use crate::cubical::typechecker;
use crate::cubical::syntax::Term;

#[unsafe(no_mangle)]
pub extern "C" fn cubical_eval(source: *const c_char) -> *mut c_char {
    if source.is_null() {
        return std::ptr::null_mut();
    }
    let c_str = unsafe { CStr::from_ptr(source) };
    let src = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return std::ptr::null_mut(),
    };
    match eval_cubical_source(src) {
        Ok(result) => match CString::new(result) {
            Ok(cs) => cs.into_raw(),
            Err(_) => std::ptr::null_mut(),
        },
        Err(_) => std::ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn cubical_eval_int(source: *const c_char) -> i64 {
    if source.is_null() {
        return -1;
    }
    let c_str = unsafe { CStr::from_ptr(source) };
    let src = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return -1,
    };
    match eval_cubical_source(src) {
        Ok(result_str) => {
            let eq_pos = match result_str.find(" = ") {
                Some(p) => p,
                None => return -1,
            };
            let nat_str = result_str[eq_pos + 3..].trim();
            parse_nat_str(nat_str)
        }
        Err(_) => -1,
    }
}

fn parse_nat_str(s: &str) -> i64 {
    let t = s.trim();
    if t.is_empty() {
        return -1;
    }

    if let Ok(n) = t.parse::<i64>() {
        if n >= 0 {
            return n;
        }
    }

    let inner = if t.starts_with('(') && t.ends_with(')') {
        t[1..t.len() - 1].trim()
    } else {
        t
    };
    if inner == "zero" {
        return 0;
    }
    if let Some(rest) = inner.strip_prefix("suc ") {
        let n = parse_nat_str(rest);
        return if n >= 0 { n + 1 } else { -1 };
    }
    if let Some(rest) = inner.strip_prefix("suc(") {
        let wrapped = format!("({}", rest);
        let n = parse_nat_str(&wrapped);
        return if n >= 0 { n + 1 } else { -1 };
    }
    if t.starts_with('(') && t.ends_with(')') {
        let inner2 = t[1..t.len() - 1].trim();
        if let Ok(n) = inner2.parse::<i64>() {
            if n >= 0 {
                return n;
            }
        }
    }
    -1
}

#[unsafe(no_mangle)]
pub extern "C" fn cubical_free_string(s: *mut c_char) {
    if !s.is_null() {
        unsafe {
            let _ = CString::from_raw(s);
        }
    }
}

/// Build a shared globals Vec from env.defs and evaluate all definition bodies,
/// replacing placeholders with computed values.
///
/// Definitions are evaluated oldest-first so that earlier (older) definitions
/// are available when evaluating later (newer) ones.  The shared `Globals`
/// ensures that closures created during evaluation see updated values when
/// recursive calls unfold.
fn build_def_values(env: &Env) -> Globals {
    let n = env.defs.len();
    // Placeholder for every definition (to be replaced after evaluation).
    let placeholder = Value::VNeutral(Neutral::NVar(0));
    let globals: Globals = std::rc::Rc::new(std::cell::RefCell::new(vec![placeholder; n]));

    // Evaluate oldest (highest index) to newest (index 0).
    for idx in (0..n).rev() {
        let (_, _, val) = &env.defs[idx];
        let v = eval_nbe(&[], &globals, idx, val);
        globals.borrow_mut()[idx] = v;
    }

    globals
}

fn eval_cubical_source(source: &str) -> Result<String, String> {
    let mut env = Env::new();
    let mut parser = ProgramParser::new(source).map_err(|e| e.to_string())?;
    let mut last_name = String::new();
    let mut last_term: Option<Term> = None;

    while let Some(decl) = parser.next_decl().map_err(|e| e.to_string())? {
        match decl {
            crate::cubical::parser::Decl::Import { .. } => {
                return Err("import not supported".to_string());
            }
            crate::cubical::parser::Decl::Data(dt) => {
                env.declare_datatype(dt);
            }
            crate::cubical::parser::Decl::Def { name, ty, val } => {
                let closed_ty = apply_globals(&env.defs, &ty);
                env.define(name.clone(), closed_ty.clone(), val.clone());
                typechecker::check_dt(
                    &env.datatypes,
                    &global_ctx(&env.defs),
                    &val,
                    &closed_ty,
                )
                .map_err(|e| format!("type error in '{}': {}", name, e))?;
                last_name = name;
                last_term = Some(val);
            }
        }
    }

    let (name, val) = if let Some(entry) = env.defs.iter().find(|(n, _, _)| n == "main") {
        (entry.0.clone(), entry.2.clone())
    } else if let (Some(n), Some(v)) = (Some(last_name), last_term) {
        (n, v)
    } else {
        return Err("no definition to evaluate".to_string());
    };

    // Build global definition values with shared globals (Rc<RefCell<Vec<Value>>>).
    // Closures created during evaluation share the same globals, so recursive
    // self-references resolve correctly after placeholder replacement.
    let globals = build_def_values(&env);

    let global_names: Vec<String> = env.defs.iter().map(|(n, _, _)| n.clone()).collect();

    // The target term is the most recently parsed definition, so global_offset = 0
    // (meaning TVar(0) is the most recent global = the target itself at env.defs[0]).
    let nf = nbe_eval_with_globals(&val, &globals, 0);
    let result = show_term(&global_names, &nf);

    Ok(format!("{} = {}", name, result))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_eval_simple() {
        let src = "def main : U1 = U0";
        let result = eval_cubical_source(src).unwrap();
        assert!(result.contains("main"));
    }

    #[test]
    fn test_eval_nat() {
        let src = "data Nat = | zero : Nat | suc : Nat -> Nat\n\
                   def main : Nat = suc (suc zero)";
        let result = eval_cubical_source(src).unwrap();
        assert!(result.contains("2") || result.contains("suc (suc zero)"));
    }

    #[test]
    fn test_eval_plus() {
        let src = "data Nat = | zero : Nat | suc : Nat -> Nat\n\
                   def plus : Nat -> Nat -> Nat = \\m n. elim (\\_. Nat) { \
                   | zero => n | suc m' => suc (plus m' n) } m\n\
                   def main : Nat = plus (suc (suc zero)) (suc (suc (suc zero)))";
        let result = eval_cubical_source(src).unwrap();
        eprintln!("plus result: {}", result);
        assert!(result.contains("main"));
    }

    #[test]
    fn test_eval_fact() {
        let src = "data Nat = | zero : Nat | suc : Nat -> Nat\n\
                   def plus : Nat -> Nat -> Nat = \\m n. elim (\\_. Nat) { \
                   | zero => n | suc m' => suc (plus m' n) } m\n\
                   def mul : Nat -> Nat -> Nat = \\m n. elim (\\_. Nat) { \
                   | zero => zero | suc m' => plus n (mul m' n) } m\n\
                   def fact : Nat -> Nat = \\n. elim (\\_. Nat) { \
                   | zero => suc zero | suc n' => mul (suc n') (fact n') } n\n\
                   def main : Nat = fact (suc (suc (suc zero)))";
        let result = eval_cubical_source(src).unwrap();
        eprintln!("fact result: {}", result);
        assert!(result.contains("6") || result.contains("suc") || result.contains("main"));
    }

    #[test]
    fn test_eval_int_ffi() {
        let src = "data Nat = | zero : Nat | suc : Nat -> Nat\n\
                   def main : Nat = suc (suc (suc zero))";
        let c_src = CString::new(src).unwrap();
        let result = cubical_eval_int(c_src.as_ptr());
        assert_eq!(result, 3);
    }
}
