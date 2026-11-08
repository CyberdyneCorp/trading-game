## ADDED Requirements

### Requirement: Mission and seed from the local date
The daily challenge SHALL be picked from the device's local date:
- the mission SHALL be `DAILY_POOL_V1[dayIndex mod n]`, where `dayIndex` is the number of whole civil days since 2026-01-01 and the pool is a fixed, versioned list of every built-in mission except the capstone (m12) and the round markets;
- the seed SHALL be FNV-1a 32 of `"tradelab-daily-v1:" + date`, with 0 replaced by 1.

Every player on the same local date SHALL get the same mission and seed, on web and desktop.

#### Scenario: Same challenge on both clients
- **WHEN** web and desktop compute the challenge for 2026-11-08
- **THEN** both SHALL pick the same mission and seed, as recorded in the shared parity fixture

### Requirement: Daily challenge runs
A daily challenge run SHALL use the mission's own rules, no advisor, no intel perks and the default balance. Its run record SHALL carry `daily` with the date, mission and seed. It SHALL count like free play: it MAY assess an elective stage, SHALL never be a campaign leg, and SHALL earn no advisor points. Retries SHALL be allowed: the first finished attempt is the day's record, and the best attempt is shown beside it.

#### Scenario: First and best
- **WHEN** a player finishes the day's challenge with score 62 and then retries with score 75
- **THEN** the history SHALL show 62 as the day's first attempt and 75 as the best

### Requirement: Personal-best history without a leaderboard
The client SHALL keep the last 90 days of daily results on the device (mission, seed, first and best attempt with score and P&L, attempt count), ranked by the mission score, never by P&L. There SHALL be no streak counter, no leaderboard and no ranking against other players.

#### Scenario: No ranking
- **WHEN** the daily challenge screen is shown
- **THEN** it SHALL show only the player's own history and SHALL NOT show other players' results or a streak
