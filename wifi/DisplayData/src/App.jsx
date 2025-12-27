import React, { useEffect, useState } from "react";
import axios from "axios";
import "./App.css";

export default function FirebaseTable() {
  const [rows, setRows] = useState([]);

  useEffect(() => {
    fetchData();
    const interval = setInterval(fetchData, 100);   // refresh every 10 sec
    return () => clearInterval(interval);
  }, []);

  const fetchData = async () => {
    try {
      const res = await axios.get(
        `https://hallo-esp-default-rtdb.firebaseio.com/sensorLogs/device1.json`
      );
      console.log("res",res.data);
      if (!res.data) return;

      const arr = Object.values(res.data)
      .sort((a, b) => new Date(b.timestamp) - new Date(a.timestamp)) // newest first
      .slice(0, 10);   // 👈 ONLY 10 LATEST
      setRows(arr);
    } catch (err) {
      console.error(err);
    }
  };

  return (
    <div className="table-container">
      <h2>Prawn Farm Water Quality Logs</h2>
      <table>
        <thead>
          <tr>
            <th>Timestamp</th>
            <th>Temp (°C)</th>
            <th>TDS (ppm)</th>
            <th>pH</th>
            <th>ORP (mV)</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <tr key={i}>
              <td >{r.timestamp}</td>
              <td>{r.temperature}</td>
              <td>{r.tds}</td>
              <td>{r.ph}</td>
              <td>{r.orp}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
