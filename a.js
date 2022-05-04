(() => {
/**
 * The base implementation of `getTag` without fallbacks for buggy environments.
 *
 * @private
 * @param {*} value The value to query.
 * @returns {string} Returns the `toStringTag`.
 */
function baseGetTag(value) {
  const objectProto = Object.prototype
 const hasOwnProperty = objectProto.hasOwnProperty
  const toString = objectProto.toString
  const symToStringTag = typeof Symbol != 'undefined' ? Symbol.toStringTag : undefined
  if (value == null) {
    return value === undefined ? '[object Undefined]' : '[object Null]'
  }
  if (!(symToStringTag && symToStringTag in Object(value))) {
    return toString.call(value)
  }
  const isOwn = hasOwnProperty.call(value, symToStringTag)
  const tag = value[symToStringTag]
  let unmasked = false
  try {
    value[symToStringTag] = undefined
    unmasked = true
  } catch (e) {}

  const result = toString.call(value)
  if (unmasked) {
    if (isOwn) {
      value[symToStringTag] = tag
    } else {
      delete value[symToStringTag]
    }
  }
  return result
}

// console.log(baseGetTag(Object.defineProperty))
// console.log(baseGetTag(DataView))
// console.log(baseGetTag(Map))
// console.log(baseGetTag(Promise))
// console.log(baseGetTag(Set))
// console.log(baseGetTag(WeakMap))
// console.log(baseGetTag(Object.create))
// console.log(baseGetTag(new DataView(new ArrayBuffer(1))))
console.log(baseGetTag(new Map))
// console.log(baseGetTag(Promise.resolve()))
// console.log(baseGetTag(new Set))
// console.log(baseGetTag(new WeakMap))
// console.log(baseGetTag((function () { return arguments; })()))

// function Fn(e){if(rs(e)&&!Ka(e)&&!(e instanceof Wn)){if(e instanceof jn)return e;if(Ge.call(e,"__wrapped__"))return Fo(e)}return new jn(e)}

// console.log(baseGetTag(Fn))

//                 Fn.templateSettings = {
//                     escape: /<%-([\s\S]+?)%>/g,
//                     evaluate: /<%([\s\S]+?)%>/g,
//                     interpolate: /<%=([\s\S]+?)%>/g,
//                     variable: "",
//                     imports: {
//                         _: Fn
//                     }
//                 };
// console.log(baseGetTag(Fn.templateSettings))
})();
