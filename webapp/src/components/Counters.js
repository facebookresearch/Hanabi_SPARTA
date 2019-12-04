//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import { BoardContext } from "../BoardState";
import { Sticky } from "react-sticky";
import cx from "classnames";

function usePrevious(value) {
  const ref = React.useRef();
  React.useEffect(() => {
    ref.current = value;
  });
  return ref.current;
}

function useDidChange(val) {
  const prevVal = usePrevious(val);
  return prevVal !== val;
}

function Counters() {
  const { gameState: state } = React.useContext(BoardContext);
  console.log("new state", JSON.stringify(state.hintsLog));

  const [showAnimation, setShowAnimation] = React.useState(false);

  const defused = useDidChange(state.defuseCounter);
  React.useEffect(() => {
    if (defused && state.defuseCounter !== 3) {
      setShowAnimation(true);
      setTimeout(() => setShowAnimation(false), 800);
    }
  }, [defused, setShowAnimation, state.defuseCounter]);

  return (
    <Sticky>
      {({ style, isSticky }) => (
        <div
          style={style}
          className={cx("area horizontal", { sticky: isSticky })}
        >
          <div className={cx("enlargeable", { enlarge: showAnimation })}>
            <div
              className={cx("bombs", {
                "shake-hard": showAnimation,
                "shake-constant": showAnimation
              })}
            >
              <h3>Defuses Left</h3>
              {new Array(state.defuseCounter).fill(1).map((_, idx) => (
                <svg
                  key={idx}
                  className="hint-counter"
                  style={{ marginRight: 5 }}
                  width={40}
                  viewBox="0 0 100 100"
                  xmlns="http://www.w3.org/2000/svg"
                >
                  <circle cx="50" cy="50" r="50" fill="#ea2b2b" />
                </svg>
              ))}
              {new Array(3 - state.defuseCounter).fill(1).map((_, idx) => (
                <svg
                  key={idx}
                  className="hint-counter empty"
                  style={{ marginRight: 5 }}
                  width={40}
                  viewBox="0 0 100 100"
                  xmlns="http://www.w3.org/2000/svg"
                >
                  <circle
                    cx="50"
                    cy="50"
                    r="50"
                    stroke="#aaa"
                    strokeDasharray="10"
                    strokeWidth="5%"
                    fill="transparent"
                  />
                </svg>
              ))}
            </div>
          </div>
          <div className="hints">
            <h3>Hints Left</h3>
            {new Array(state.hintCounter).fill(1).map((_, idx) => (
              <svg
                key={idx}
                className="hint-counter"
                style={{ marginRight: 5 }}
                width={40}
                viewBox="0 0 100 100"
                xmlns="http://www.w3.org/2000/svg"
              >
                <circle cx="50" cy="50" r="50" fill="#1e80ef" />
              </svg>
            ))}
            {new Array(8 - state.hintCounter).fill(1).map((_, idx) => (
              <svg
                key={idx}
                className="hint-counter empty"
                style={{ marginRight: 5 }}
                width={40}
                viewBox="0 0 100 100"
                xmlns="http://www.w3.org/2000/svg"
              >
                <circle
                  cx="50"
                  cy="50"
                  r="50"
                  stroke="#aaa"
                  strokeDasharray="10"
                  strokeWidth="5%"
                  fill="transparent"
                />
              </svg>
            ))}
          </div>
        </div>
      )}
    </Sticky>
  );
}

export default Counters;
