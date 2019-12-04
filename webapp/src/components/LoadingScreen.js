//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";

function LoadingScreen({ tryConnect, message }) {
  const connectBtnRef = React.useRef(null);
  React.useEffect(() => {
    connectBtnRef.current.focus();
  }, []);

  return (
    <div
      style={{
        position: "absolute",
        top: 0,
        bottom: 0,
        left: 0,
        right: 0,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        flexDirection: "column"
      }}
    >
      <button ref={connectBtnRef} onClick={tryConnect} className="connect">
        Connect
      </button>
      <div style={{ height: 30, marginTop: 10 }}>{message}</div>
    </div>
  );
}
export default LoadingScreen;
