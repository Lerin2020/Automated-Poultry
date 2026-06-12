import { useState, useCallback, useEffect, useRef } from 'react';
import mqtt from 'mqtt';

const BROKER_KEY = 'poultry-broker-host';
// When served from ESP32, connect to the same host; fallback for dev
const DEFAULT_HOST = (typeof window !== 'undefined' && window.location.hostname && window.location.hostname !== 'localhost')
  ? window.location.hostname
  : 'poultry.local';
const WS_PORT = 81;

export const useMQTT = () => {
  const [isBrokerConnected, setIsBrokerConnected] = useState(false);
  const [isESPOnline, setIsESPOnline] = useState(false);
  const [brokerHost, setBrokerHost] = useState(() => localStorage.getItem(BROKER_KEY) || DEFAULT_HOST);
  const [heartbeatData, setHeartbeatData] = useState({
    heap: 0, rssi: 0, temp: 0, humidity: 0, rtc: '', queue: 0, dht_ok: false, ip: ''
  });
  const clientRef = useRef(null);
  const subscribersRef = useRef(new Map());
  const anyMessageListeners = useRef([]);

  const connectTo = useCallback((host) => {
    // Clean up old connection
    if (clientRef.current) {
      clientRef.current.end(true);
      clientRef.current = null;
    }

    setIsBrokerConnected(false);
    setIsESPOnline(false);

    // Match the working Boat Interface pattern exactly
    const mqttClient = mqtt.connect(`ws://${host}:${WS_PORT}`, {
      reconnectPeriod: 3000,
      connectTimeout: 4000,
    });

    mqttClient.on('connect', () => {
      console.log(`Connected to ESP32 broker at ${host}:${WS_PORT}`);
      setIsBrokerConnected(true);
      setIsESPOnline(true);

      // Subscribe to system heartbeat
      mqttClient.subscribe('poultry/system/status');

      // Re-subscribe existing listeners
      subscribersRef.current.forEach((cbSet, topic) => {
        mqttClient.subscribe(topic);
      });

      // Auto-sync RTC with browser's LOCAL time
      // RTC has no timezone — store local time directly
      const tzOffsetSec = new Date().getTimezoneOffset() * -60; // UTC+1 → +3600
      const epoch = Math.floor(Date.now() / 1000) + tzOffsetSec;
      mqttClient.publish('poultry/config/cmd', JSON.stringify({ action: 'sync_rtc', epoch }));
      console.log(`[RTC] Auto-synced to local time (offset: ${tzOffsetSec}s)`);
    });

    mqttClient.on('message', (topic, message) => {
      const payload = message.toString();

      if (topic === 'poultry/system/status') {
        try {
          const m = JSON.parse(payload);
          if (m.status === 'online') {
            setIsESPOnline(true);
            setHeartbeatData({
              heap: m.heap || 0,
              rssi: m.rssi || 0,
              temp: m.temp || 0,
              humidity: m.humidity || 0,
              rtc: m.rtc || '',
              queue: m.queue || 0,
              dht_ok: m.dht_ok || false,
              ip: m.ip || '',
            });
          } else {
            setIsESPOnline(false);
          }
        } catch (e) {}
      }

      // Fire topic-specific subscribers
      const callbackSet = subscribersRef.current.get(topic);
      if (callbackSet) {
        // Wrap in Paho-like message object for compatibility with AdminPanel
        const msgObj = { destinationName: topic, payloadString: payload };
        callbackSet.forEach(cb => cb(msgObj));
      }

      // Fire any-message listeners
      anyMessageListeners.current.forEach(cb => cb(topic, payload));
    });

    mqttClient.on('close', () => {
      setIsBrokerConnected(false);
      setIsESPOnline(false);
    });

    mqttClient.on('reconnect', () => {
      console.log(`Reconnecting to ${host}...`);
    });

    mqttClient.on('error', (err) => {
      console.error('MQTT error:', err);
    });

    clientRef.current = mqttClient;
  }, []);

  // Connect on mount and when host changes
  useEffect(() => {
    connectTo(brokerHost);
    return () => {
      if (clientRef.current) {
        clientRef.current.end(true);
      }
    };
  }, [brokerHost, connectTo]);

  const changeBrokerHost = useCallback((newHost) => {
    localStorage.setItem(BROKER_KEY, newHost);
    setBrokerHost(newHost);
  }, []);

  const publish = useCallback((topic, payload) => {
    if (clientRef.current && clientRef.current.connected) {
      clientRef.current.publish(topic, payload);
    } else {
      console.warn('Not connected to ESP32 broker.');
    }
  }, []);

  const subscribe = useCallback((topic, callback) => {
    if (!subscribersRef.current.has(topic)) {
      subscribersRef.current.set(topic, new Set());
    }
    subscribersRef.current.get(topic).add(callback);

    if (clientRef.current && clientRef.current.connected) {
      clientRef.current.subscribe(topic);
    }

    return () => {
      const cbSet = subscribersRef.current.get(topic);
      if (cbSet) {
        cbSet.delete(callback);
        if (cbSet.size === 0) {
          subscribersRef.current.delete(topic);
          if (clientRef.current && clientRef.current.connected) {
            clientRef.current.unsubscribe(topic);
          }
        }
      }
    };
  }, []);

  const onAnyMessage = useCallback((callback) => {
    anyMessageListeners.current.push(callback);
    return () => {
      anyMessageListeners.current = anyMessageListeners.current.filter(cb => cb !== callback);
    };
  }, []);

  return { isBrokerConnected, isESPOnline, heartbeatData, publish, subscribe, onAnyMessage, brokerHost, changeBrokerHost };
};
