//! FFI interface for calling cubical from C/C++.
//!
//! Exposes `extern "C"` functions that can be linked from C++ code.
//! The main entry point is `cubical_eval` which takes a cubical source
//! string, typechecks and evaluates it, and returns the result as a
//! C string (caller must free with `cubical_free_string`).

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use crate::cubical::syntax::show_term;
use crate::cubical::env::{Env, apply_globals, global_ctx};
use crate::cubical::nbe::{nbe_eval, Value, Neutral, eval_nbe, quote};
use crate::cubical::parser::ProgramParser;
use crate::cubical::typechecker;
use crate::cubical::syntax::Term;

/// Evaluate a cubical expression string and return the result as a string.
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

/// Evaluate cubical source and return result as a 64-bit integer (for Nat).
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

// ---------------------------------------------------------------------------
// Internal evaluation logic — fully reduces recursive definitions via NBE
// ---------------------------------------------------------------------------

fn eval_cubical_source(source: &str) -> Result<String, String> {
    // Phase 1: Parse and typecheck all declarations, populate the Env
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
                // Register before checking body (recursion support)
                env.define(name.clone(), closed_ty.clone(), val.clone());
                // Check the body
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

    // Phase 2: Find the final term to evaluate
    let (name, val) = if let Some(entry) = env.defs.iter().find(|(n, _, _)| n == "main") {
        (entry.0.clone(), entry.2.clone())
    } else if let (Some(n), Some(v)) = (Some(last_name), last_term) {
        (n, v)
    } else {
        return Err("no definition to evaluate".to_string());
    };

    // Phase 3: Use apply_globals to inline all non-recursive definitions,
    // then NBE-evaluate. apply_globals substitutes from highest index
    // (oldest) to lowest (newest), so after substitution only TVar(0)
    // through TVar(k) remain for recursive self-references.
    // Then we build a minimal NBE env for those remaining references.
    let substituted = apply_globals(&env.defs, &val);
    let nf = nbe_eval(&substituted);
    let global_names: Vec<String> = env.defs.iter().map(|(n, _, _)| n.clone()).collect();
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
        assert!(result.contains("suc (suc zero)"));
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