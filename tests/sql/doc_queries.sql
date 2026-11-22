-- Example queries of SQL.md, run by CI (and by hand) on a database loaded with:
--   asterix -Q -P -f install/sample_data/cat_034_048.pcap
--   asterix -Q -R -f install/sample_data/cat_001_002.pcap
--   asterix -Q -P -f install/sample_data/asterix.pcap
--   asterix -Q -P -f install/sample_data/cat_062_065.pcap
-- Each query is preceded by a "-- Qn:" line; SQL.md shows the same text and the rows it returns.

-- Q1: every CAT048 message with all its tables joined (first occurrence of each BDS register, first
-- element of the other repetitive items)
SELECT c.rec_id, c.I010_SAC AS sac, c.I010_SIC AS sic, c.I161_Tn AS track, c.I220_ACAddr AS icao,
       c.I240_TId AS callsign, c.I070_Mode3A AS squawk, c.I090_FL AS fl,
       b40.MCP_ALT AS sel_alt, b44.WIND_SPEED AS wind, b50.TAS AS tas, b60.IAS AS ias, b60.MACH AS mach,
       b0.BDS AS other_bds, rds.DOP AS doppler, sp.SP AS sp, re.VAL AS re
FROM cat048 c
LEFT JOIN cat048_I250_BDS40 b40 ON b40.rec_id = c.rec_id AND b40.reg_idx = 0
LEFT JOIN cat048_I250_BDS44 b44 ON b44.rec_id = c.rec_id AND b44.reg_idx = 0
LEFT JOIN cat048_I250_BDS45 b45 ON b45.rec_id = c.rec_id AND b45.reg_idx = 0
LEFT JOIN cat048_I250_BDS50 b50 ON b50.rec_id = c.rec_id AND b50.reg_idx = 0
LEFT JOIN cat048_I250_BDS60 b60 ON b60.rec_id = c.rec_id AND b60.reg_idx = 0
LEFT JOIN cat048_I250_BDS0  b0  ON b0.rec_id  = c.rec_id AND b0.reg_idx  = 0
LEFT JOIN cat048_I120_RDS   rds ON rds.rec_id = c.rec_id AND rds.rep_idx = 0
LEFT JOIN cat048_ISP        sp  ON sp.rec_id  = c.rec_id AND sp.rep_idx  = 0
LEFT JOIN cat048_IRE        re  ON re.rec_id  = c.rec_id AND re.rep_idx  = 0
ORDER BY c.rec_id
LIMIT 8;

-- Q2: every message of every category, in time order
SELECT * FROM (
          SELECT 'CAT001' AS category, rec_id, timestamp, I010_SAC AS sac, I010_SIC AS sic FROM cat001
UNION ALL SELECT 'CAT002', rec_id, timestamp, I010_SAC, I010_SIC FROM cat002
UNION ALL SELECT 'CAT034', rec_id, timestamp, I010_SAC, I010_SIC FROM cat034
UNION ALL SELECT 'CAT048', rec_id, timestamp, I010_SAC, I010_SIC FROM cat048
UNION ALL SELECT 'CAT062', rec_id, timestamp, I010_SAC, I010_SIC FROM cat062
) m
ORDER BY timestamp, category, rec_id
LIMIT 8;

-- Q3: a view with every BDS register of CAT048 I250, whatever its table
CREATE OR REPLACE VIEW cat048_bds AS
          SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS40
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS44
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS45
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS50
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS60
UNION ALL SELECT rec_id, rep_idx, reg_idx, BDS FROM cat048_I250_BDS0;

-- Q4: number of BDS registers in each message, per aircraft, in the order they were received
SELECT c.I220_ACAddr AS icao, c.I240_TId AS callsign, c.rec_id, c.I140_ToD AS tod,
       COUNT(b.BDS) AS bds_count, GROUP_CONCAT(b.BDS ORDER BY b.rep_idx) AS registers
FROM cat048 c
LEFT JOIN cat048_bds b ON b.rec_id = c.rec_id
WHERE c.I220_ACAddr IN ('400C4A', '3D0CDE', '3C660C')
GROUP BY c.rec_id, c.I220_ACAddr, c.I240_TId, c.I140_ToD
ORDER BY icao, tod, c.rec_id;

-- Q5: per aircraft: messages, BDS registers received and which ones
SELECT m.icao, MAX(m.callsign) AS callsign, COUNT(*) AS messages, SUM(m.bds_count) AS bds_total,
       MAX(m.bds_count) AS max_per_message, ROUND(AVG(m.bds_count), 2) AS avg_per_message,
       GROUP_CONCAT(DISTINCT m.registers ORDER BY m.registers SEPARATOR ' | ') AS register_sets
FROM (SELECT c.rec_id, c.I220_ACAddr AS icao, c.I240_TId AS callsign, COUNT(b.BDS) AS bds_count,
             GROUP_CONCAT(b.BDS ORDER BY b.rep_idx) AS registers
      FROM cat048 c
      LEFT JOIN cat048_bds b ON b.rec_id = c.rec_id
      GROUP BY c.rec_id, c.I220_ACAddr, c.I240_TId) m
GROUP BY m.icao
ORDER BY bds_total DESC, m.icao
LIMIT 10;

-- Q6: selected altitude (BDS 4,0) and air data (BDS 6,0) of one aircraft, plot by plot
SELECT c.I140_ToD AS tod, c.I240_TId AS callsign, c.I090_FL AS fl,
       CASE WHEN b40.MCP_ALT_STATUS = 1 THEN b40.MCP_ALT END AS sel_alt,
       CASE WHEN b40.BP_STATUS = 1 THEN b40.BP END AS baro,
       b60.HDG AS mag_hdg, b60.IAS AS ias, b60.MACH AS mach, b60.IVV AS ivv
FROM cat048 c
LEFT JOIN cat048_I250_BDS40 b40 ON b40.rec_id = c.rec_id AND b40.reg_idx = 0
LEFT JOIN cat048_I250_BDS60 b60 ON b60.rec_id = c.rec_id AND b60.reg_idx = 0
WHERE c.I240_TId = 'AEE2BR'
ORDER BY tod;

-- Q7: plots that carry the same BDS register more than once
SELECT c.rec_id, c.I240_TId AS callsign, b.BDS, b.rep_idx, b.reg_idx
FROM cat048 c
JOIN cat048_bds b ON b.rec_id = c.rec_id
WHERE b.reg_idx > 0;

-- Q8: CAT001 track quality (I210, a list of parts in its own table)
SELECT c.I161_tpn AS track, c.I070_Mode3A AS squawk, c.I090_ModeC AS fl,
       GROUP_CONCAT(q.Qty ORDER BY q.rep_idx) AS quality
FROM cat001 c
LEFT JOIN cat001_I210 q ON q.rec_id = c.rec_id
GROUP BY c.rec_id, c.I161_tpn, c.I070_Mode3A, c.I090_ModeC
ORDER BY c.rec_id;

-- Q9: CAT062 tracks: compound subfields are columns of the category table (I380_ID_ACID, I380_ADR_ADR)
SELECT I040_TrkN AS track, I070_ToT AS tot, I380_ID_ACID AS callsign, I380_ADR_ADR AS icao,
       I105_Lat AS lat, I105_Lon AS lon, I136_MFL AS fl, I380_SAL_Alt AS sel_alt, I380_COM_COM AS com
FROM cat062
WHERE I380_ADR_ADR IS NOT NULL
ORDER BY tot, track;

-- Q10: plots per radar and category
SELECT category, sac, sic, COUNT(*) AS messages FROM (
          SELECT 'CAT001' AS category, I010_SAC AS sac, I010_SIC AS sic FROM cat001
UNION ALL SELECT 'CAT034', I010_SAC, I010_SIC FROM cat034
UNION ALL SELECT 'CAT048', I010_SAC, I010_SIC FROM cat048
UNION ALL SELECT 'CAT062', I010_SAC, I010_SIC FROM cat062
) m
GROUP BY category, sac, sic
ORDER BY category, sac, sic;

-- Q11: deleting old plots also deletes their child rows (FOREIGN KEY ... ON DELETE CASCADE)
START TRANSACTION;
SELECT (SELECT COUNT(*) FROM cat048) AS plots, (SELECT COUNT(*) FROM cat048_bds) AS bds_rows;
DELETE FROM cat048 WHERE I140_ToD < 27355;
SELECT (SELECT COUNT(*) FROM cat048) AS plots, (SELECT COUNT(*) FROM cat048_bds) AS bds_rows;
ROLLBACK;
