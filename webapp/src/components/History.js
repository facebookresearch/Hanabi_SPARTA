//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import cx from "classnames";
import { BoardContext } from "../BoardState";

function History() {
  const {
    gameState: { history, isPlayerTurn, gameOver }
  } = React.useContext(BoardContext);

  const logContainerRef = React.useRef(null);
  const scrollToBottom = React.useCallback(
    () =>
      setTimeout(() => {
        if (logContainerRef.current)
          logContainerRef.current.scrollTop =
            logContainerRef.current.scrollHeight;
      }, 0),
    [logContainerRef]
  );
  React.useEffect(() => {
    scrollToBottom();
  }, [scrollToBottom, history.length]);

  return (
    <div className="log-wrapper">
      {!isPlayerTurn && !gameOver ? (
        <div
          style={{
            padding: "0px 0 5px",
            fontWeight: "bold",
            fontSize: 20,
            borderBottom: "1px solid rgba(255,255,255,0.25)",
            marginBottom: 10
          }}
        >
          <div className="loading-icon" style={{ marginRight: 6 }}></div>
          Loading...
        </div>
      ) : null}
      <div style={{ padding: "0px 0 5px" }}>Log:</div>
      {history.length > 0 ? (
        <div className="log-scroll" ref={logContainerRef}>
          {[...history].map((log, idx) => (
            <span
              key={idx}
              className={cx("log-row", {
                bright: idx === history.length - 1
              })}
            >
              {log}
            </span>
          ))}
        </div>
      ) : (
        <div style={{ fontStyle: "italic", opacity: 0.3 }}>Empty</div>
      )}
    </div>
  );
}

export default History;
