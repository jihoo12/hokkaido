//! FFI interface for calling cubical from C/C++.
//!
//! Exposes `extern "C"` functions that can be linked from C++ code.
//! The main entry point is `cubical_eval` which takes a cubical source
//! string, typechecks and evaluates it, and returns the result as a
//! C string (caller must free with `cubical_free_string`).

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

use crate::cubical::env::Env;
use crate::cubical::nbe::nbe_eval;
use crate::cubical::parser::ProgramParser;
use crate::cubical::syntax::show_term;
use crate::cubical::typechecker::check_dt;

/// Evaluate a cubical expression string and return the result as a string.
///
/// The input should be a complete cubical program (definitions + expression).
/// The last definition (or `main` if present) is evaluated and its normal form
/// is returned as a string.
///
/// Returns a C string that must be freed with `cubical_free_string`.
/// Returns null on error (parse or type error).
#[no_mangle]
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
        Ok(result) => {
            match CString::new(result) {
                Ok(cs) => cs.into_raw(),
                Err(_) => std::ptr::null_mut(),
            }
        }
        Err(_) => std::ptr::null_mut(),
    }
}

/// Evaluate a cubical expression and return the result as a 64-bit integer.
///
/// The expression must evaluate to a natural number (Nat) in normal form,
/// i.e. `suc (suc (suc ... zero))`. Returns -1 on error or if the result
/// is not a natural number.
#[no_mangle]
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
            // Parse the result as a Nat: count 'suc' prefixes
            let trimmed = result_str.trim();
            if trimmed == "zero" {
                return 0;
            }
            // Count "suc " or "suc(" prefixes
            let mut count: i64 = 0;
            let mut remaining = trimmed;
            loop {
                if remaining.starts_with("suc ") {
                    count += 1;
                    remaining = &remaining[4..];
                } else if remaining.starts_with("suc(") {
                    count += 1;
                    remaining = &remaining[4..];
                } else if remaining == "zero" {
                    return count;
                } else {
                    return -1; // Not a Nat
                }
            }
        }
        Err(_) => -1,
    }
}

/// Free a string returned by `cubical_eval`.
#[no_mangle]
pub extern "C" fn cubical_free_string(s: *mut c_char) {
    if !s.is_null() {
        unsafe {
            let _ = CString::from_raw(s);
        }
    }
}

// ---------------------------------------------------------------------------
// Internal evaluation logic
// ---------------------------------------------------------------------------

fn eval_cubical_source(source: &str) -> Result<String, String> {
    let mut env = Env::new();
    let mut parser = ProgramParser::new(source).map_err(|e| e.to_string())?;

    let mut last_name = String::new();
    let mut last_ty = None;
    let mut last_val = None;

    while let Some(decl) = parser.next_decl().map_err(|e| e.to_string())? {
        match decl {
            crate::cubical::parser::Decl::Import { path: _ } => {
                return Err("import not supported in FFI eval".to_string());
            }
            crate::cubical::parser::Decl::Data(dt) => {
                env.declare_datatype(dt);
            }
            crate::cubical::parser::Decl::Def { name, ty, val } => {
                // Resolve globals in the type
                let closed_ty = crate::cubical::env::apply_globals(&env.defs, &ty);
                let closed_val = val.clone();

                // Check the type is a universe
                let ty_nf = nbe_eval(&closed_ty);
                let inferred = crate::cubical::typechecker::infer_dt(&env.datatypes, &crate::cubical::env::global_ctx(&env.defs), &ty_nf)
                    .map_err(|e| format!("type error in '{}': {}", name, e))?;
                match nbe_eval(&inferred) {
                    crate::cubical::syntax::Term::TUniv(_) => {}
                    other => return Err(format!("expected universe, got: {}", other)),
                }

                // Register before checking body (for recursion)
                env.define(name.clone(), closed_ty.clone(), closed_val.clone());

                // Check the body
                crate::cubical::typechecker::check_dt(
                    &env.datatypes,
                    &crate::cubical::env::global_ctx(&env.defs),
                    &closed_val,
                    &closed_ty,
                )
                .map_err(|e| format!("type error in '{}': {}", name, e))?;

                last_name = name;
                last_ty = Some(closed_ty);
                last_val = Some(closed_val);
            }
        }
    }

    // Prefer `main` over the last definition
    let (name, val) = if let Some(main_entry) = env.defs.iter().find(|(n, _, _)| n == "main") {
        (main_entry.0.clone(), main_entry.2.clone())
    } else if let (Some(name), Some(val)) = (Some(last_name), last_val) {
        (name, val)
    } else {
        return Err("no definition to evaluate".to_string());
    };

    let nf = nbe_eval(&val);
    let global_names: Vec<String> = env.defs.iter().map(|(n, _, _)| n.clone()).collect();
    let result = show_term(&global_names, &nf);

    Ok(format!("{} = {}", name, result))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_eval_simple() {
        let src = "def id : (A : U0) -> A -> A = \\A x. x\ndef main : U0 -> U0 = id U0";
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
    fn test_eval_int_ffi() {
        let src = "data Nat = | zero : Nat | suc : Nat -> Nat\n\
                   def main : Nat = suc (suc (suc zero))";
        let c_src = CString::new(src).unwrap();
        let result = cubical_eval_int(c_src.as_ptr());
        assert_eq!(result, 3);
    }
}