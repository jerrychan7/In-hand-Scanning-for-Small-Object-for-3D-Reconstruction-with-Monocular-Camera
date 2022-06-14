import { useCallback, useEffect, useRef } from "react";

export function useAnimateFrame(callback, deps) {
  const timerRef = useRef(null), previousTimestamp = useRef(0);
  const animate = useCallback(time => {
    let dt = time - previousTimestamp.current;
    previousTimestamp.current = time;
    callback(time, dt);
    timerRef.current = requestAnimationFrame(animate);
  }, deps);
  const cancel = () => cancelAnimationFrame(timerRef.current);
  useEffect(() => {
    timerRef.current = requestAnimationFrame(animate);
    return cancel;
  }, [animate]);
  return cancel;
};
