//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import { BoardContext } from "../BoardState";
import cx from "classnames";

function ShowHints({ hints }) {
  const {
    state: { colors }
  } = React.useContext(BoardContext);

  const numbers = [1, 2, 3, 4, 5];

  return (
    <div style={{ marginTop: 10 }}>
      {colors.map((color, cIdx) => {
        const colorChar = color[0];
        return (
          <div
            className={colorChar}
            key={color}
            style={{ fontSize: 10, margin: "0px 10px", padding: "0px 2px" }}
          >
            <span
              style={{
                width: 10,
                display: "inline-block",
                textAlign: "right",
                marginRight: 8
              }}
            >
              {color[0]}:
            </span>
            {numbers.map(number => (
              <span
                style={{ marginRight: 2, display: "inline-block" }}
                key={number}
                className={cx({ nope: hints[cIdx * 5 + number - 1] === 0 })}
              >
                {number}
              </span>
            ))}
          </div>
        );
      })}
    </div>
  );
}

export default ShowHints;
