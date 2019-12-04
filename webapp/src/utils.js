//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

export function debounce(func, wait, immediate) {
  var timeout;

  return function executedFunction() {
    var context = this;
    var args = arguments;

    var later = function() {
      timeout = null;
      if (!immediate) func.apply(context, args);
    };

    var callNow = immediate && !timeout;
    clearTimeout(timeout);
    timeout = setTimeout(later, wait);

    if (callNow) func.apply(context, args);
  };
}

export function throttleAndDropSubsequent(func, limit) {
  let inThrottle;
  return function() {
    const args = arguments;
    const context = this;
    if (!inThrottle) {
      func.apply(context, args);
      inThrottle = true;
      setTimeout(() => (inThrottle = false), limit);
    }
  };
}

export function throttle(func, waitTime) {
  var funcQueue = [];
  var isWaiting;

  var executeFunc = function(params) {
    isWaiting = true;
    func(params);
    setTimeout(play, waitTime);
  };

  var play = function() {
    isWaiting = false;
    if (funcQueue.length) {
      var params = funcQueue.shift();
      executeFunc(params);
    }
  };

  return function(params) {
    if (isWaiting) {
      funcQueue.push(params);
    } else {
      executeFunc(params);
    }
  };
}
