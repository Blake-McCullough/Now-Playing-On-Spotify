#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>  // For XC4630 display

// ============================================
// CONFIGURATION - CHANGE THESE VALUES
// ============================================
const char* WIFI_SSID = "your_wifi_ssid";
const char* WIFI_PASSWORD = "your_wifi_password";

const char* CLIENT_ID = "your_client_id";
const char* CLIENT_SECRET = "your_client_secret";

// Display dimensions
#define TFT_WIDTH  320
#define TFT_HEIGHT 240

// Initialize display
MCUFRIEND_kbv tft;

// Spotify credentials storage
Preferences preferences;
String accessToken;
String refreshToken;

// Current track information
String currentTrackName = "";
String currentArtistName = "";
String currentAlbumName = "";
int currentProgressMs = 0;
int currentDurationMs = 0;
bool isPlaying = false;
String currentAlbumArtUrl = "";

// Track polling
unsigned long lastApiCall = 0;
const unsigned long API_CALL_INTERVAL = 5000;  // Check API every 5 seconds
unsigned long lastProgressUpdate = 0;
const unsigned long PROGRESS_UPDATE_INTERVAL = 1000;  // Update progress bar every second

// Track change detection
String lastTrackId = "";

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=========================================");
    Serial.println("Spotify Now Playing Display for XC4630");
    Serial.println("=========================================\n");
    
    // Initialize display
    setupDisplay();
    
    // Initialize Preferences
    preferences.begin("spotify", false);
    
    // Connect to WiFi
    connectWiFi();
    
    // Load tokens and authenticate
    loadTokens();
    
    if (refreshToken.length() == 0) {
        showSetupScreen();
        waitForRefreshToken();
    } else {
        showMessage("Spotify Ready", "Connecting...");
        refreshAccessToken();
        if (accessToken.length() > 0) {
            showMessage("Spotify Ready", "Connected!");
            delay(1500);
        }
    }
    
    // Clear screen for main display
    tft.fillScreen(TFT_BLACK);
}

void loop() {
    unsigned long currentTime = millis();
    
    // Check Spotify API every 5 seconds
    if (currentTime - lastApiCall >= API_CALL_INTERVAL) {
        lastApiCall = currentTime;
        getCurrentlyPlaying();
    }
    
    // Update progress bar every second (if playing)
    if (isPlaying && (currentTime - lastProgressUpdate >= PROGRESS_UPDATE_INTERVAL)) {
        lastProgressUpdate = currentTime;
        
        // Increment progress (simulate elapsed time since last API call)
        currentProgressMs += PROGRESS_UPDATE_INTERVAL;
        if (currentProgressMs > currentDurationMs) {
            currentProgressMs = currentDurationMs;
        }
        
        // Update the progress bar display
        updateProgressDisplay();
    }
    
    delay(100);  // Small delay to prevent watchdog issues
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================

void setupDisplay() {
    Serial.println("Initializing XC4630 Display...");
    
    uint16_t identifier = tft.readID();
    Serial.print("Display ID: 0x");
    Serial.println(identifier, HEX);
    
    tft.begin(identifier);
    tft.setRotation(1);  // Landscape orientation (320x240)
    tft.fillScreen(TFT_BLACK);
    
    // Test display
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(50, 110);
    tft.print("Initializing...");
    
    delay(1000);
}

void showMessage(String line1, String line2) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    int16_t x1, y1;
    uint16_t w, h;
    
    tft.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 80);
    tft.print(line1);
    
    tft.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, 120);
    tft.print(line2);
}

void showSetupScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, 20);
    tft.print("FIRST TIME SETUP");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 50);
    tft.print("1. Get Refresh Token from:");
    tft.setCursor(10, 70);
    tft.print("   developer.spotify.com/console");
    
    tft.setCursor(10, 100);
    tft.print("2. Scopes needed:");
    tft.setCursor(10, 120);
    tft.print("   user-read-currently-playing");
    tft.setCursor(10, 140);
    tft.print("   user-read-playback-state");
    
    tft.setCursor(10, 170);
    tft.print("3. Paste token in Serial");
    tft.setCursor(10, 190);
    tft.print("   Monitor and press Enter");
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 220);
    tft.print("Waiting for token...");
}

void waitForRefreshToken() {
    while (refreshToken.length() == 0) {
        if (Serial.available()) {
            String input = Serial.readStringUntil('\n');
            input.trim();
            
            if (input.length() > 50) {
                refreshToken = input;
                Serial.println("\n✓ Refresh token received!");
                saveTokens();
                
                showMessage("Token Saved!", "Connecting...");
                delay(1000);
                
                if (refreshAccessToken()) {
                    showMessage("Connected!", "Ready to play");
                    delay(1500);
                    tft.fillScreen(TFT_BLACK);
                } else {
                    showMessage("Token Invalid", "Check and retry");
                    delay(2000);
                    refreshToken = "";
                    showSetupScreen();
                }
            }
        }
        delay(100);
    }
}

void updateNowPlayingDisplay(String trackName, String artistName, 
                              String albumName, int progressMs, 
                              int durationMs, bool playing) {
    
    // Only redraw if track changed
    static String lastDisplayedTrack = "";
    String currentTrackKey = trackName + artistName;
    
    if (currentTrackKey != lastDisplayedTrack) {
        lastDisplayedTrack = currentTrackKey;
        
        // Clear screen for new track
        tft.fillScreen(TFT_BLACK);
        
        // Draw album art placeholder
        drawAlbumArtPlaceholder();
        
        // Draw track name (with scrolling if needed)
        drawTextWithScrolling(trackName, 10, 170, TFT_WHITE, 2);
        
        // Draw artist name
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(10, 200);
        tft.print("Artist: ");
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print(artistName);
        
        // Draw play/pause status
        drawPlayPauseIcon(playing);
    }
    
    // Always update progress bar
    drawProgressBar(progressMs, durationMs);
    
    // Show play/pause status
    drawPlayPauseIcon(playing);
}

void drawAlbumArtPlaceholder() {
    // Draw a decorative frame for album art (center, 120x120)
    int artX = (TFT_WIDTH - 120) / 2;  // Center horizontally
    int artY = 20;
    
    // Draw outer border
    tft.drawRect(artX - 2, artY - 2, 124, 124, TFT_YELLOW);
    
    // Draw inner area (note: actual album art would go here)
    tft.fillRect(artX, artY, 120, 120, TFT_DARKGREY);
    
    // Draw musical note icon in placeholder
    tft.setTextSize(3);
    tft.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    tft.setCursor(artX + 45, artY + 45);
    tft.print("♪");
    
    tft.setTextSize(2);
    tft.setCursor(artX + 38, artY + 70);
    tft.print("ALBUM");
    tft.setTextSize(1);
    tft.setCursor(artX + 35, artY + 90);
    tft.print("ARTWORK");
}

void drawTextWithScrolling(String text, int x, int y, uint16_t color, int textSize) {
    tft.setTextSize(textSize);
    tft.setTextColor(color, TFT_BLACK);
    
    // Calculate text width
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    
    // If text fits, display normally
    if (w <= TFT_WIDTH - 20) {
        tft.setCursor(x, y);
        tft.print(text);
    } else {
        // For scrolling text, we'll display truncated version
        // Full implementation would require scrolling animation
        String truncated = text.substring(0, 30) + "...";
        tft.setCursor(x, y);
        tft.print(truncated);
    }
}

void drawProgressBar(int progressMs, int durationMs) {
    int progressPercent = (progressMs * 100) / durationMs;
    int barWidth = TFT_WIDTH - 40;
    int fillWidth = (barWidth * progressPercent) / 100;
    
    int barX = 20;
    int barY = 225;
    int barHeight = 10;
    
    // Draw background bar (dark grey)
    tft.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
    
    // Draw filled portion (green when playing, yellow when paused)
    uint16_t fillColor = isPlaying ? TFT_GREEN : TFT_YELLOW;
    tft.fillRect(barX, barY, fillWidth, barHeight, fillColor);
    
    // Draw border
    tft.drawRect(barX, barY, barWidth, barHeight, TFT_WHITE);
    
    // Draw time text
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    char timeText[20];
    int progressSec = progressMs / 1000;
    int durationSec = durationMs / 1000;
    sprintf(timeText, "%02d:%02d / %02d:%02d", 
            progressSec/60, progressSec%60,
            durationSec/60, durationSec%60);
    
    tft.setCursor(barX + (barWidth - strlen(timeText) * 6) / 2, barY - 12);
    tft.print(timeText);
}

void updateProgressDisplay() {
    // Only update the progress bar and time text
    if (currentDurationMs > 0) {
        drawProgressBar(currentProgressMs, currentDurationMs);
    }
}

void drawPlayPauseIcon(bool playing) {
    int iconX = TFT_WIDTH - 35;
    int iconY = 10;
    
    if (playing) {
        // Draw pause icon (two vertical bars)
        tft.fillRect(iconX, iconY, 4, 15, TFT_GREEN);
        tft.fillRect(iconX + 8, iconY, 4, 15, TFT_GREEN);
    } else {
        // Draw play icon (triangle)
        int16_t triangleX[] = {iconX, iconX, iconX + 12};
        int16_t triangleY[] = {iconY, iconY + 14, iconY + 7};
        tft.fillTriangle(triangleX[0], triangleY[0], 
                        triangleX[1], triangleY[1], 
                        triangleX[2], triangleY[2], 
                        TFT_YELLOW);
    }
}

// ============================================
// SPOTIFY API FUNCTIONS
// ============================================

void connectWiFi() {
    Serial.print("Connecting to WiFi");
    tft.setCursor(10, 150);
    tft.print("Connecting to WiFi...");
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed!");
    }
}

void loadTokens() {
    refreshToken = preferences.getString("refresh_token", "");
    accessToken = preferences.getString("access_token", "");
    
    if (refreshToken.length() > 0) {
        Serial.println("✓ Refresh token loaded");
    }
}

void saveTokens() {
    if (refreshToken.length() > 0) {
        preferences.putString("refresh_token", refreshToken);
        Serial.println("✓ Refresh token saved");
    }
    if (accessToken.length() > 0) {
        preferences.putString("access_token", accessToken);
        Serial.println("✓ Access token saved");
    }
}

bool refreshAccessToken() {
    if (refreshToken.length() == 0) {
        Serial.println("Error: No refresh token");
        return false;
    }
    
    HTTPClient http;
    http.begin("https://accounts.spotify.com/api/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String body = "grant_type=refresh_token&refresh_token=" + refreshToken +
                  "&client_id=" + String(CLIENT_ID) + 
                  "&client_secret=" + String(CLIENT_SECRET);
    
    Serial.println("Refreshing access token...");
    int httpCode = http.POST(body);
    
    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, payload);
        
        accessToken = doc["access_token"].as<String>();
        
        if (doc.containsKey("refresh_token")) {
            String newToken = doc["refresh_token"].as<String>();
            if (newToken.length() > 0 && newToken != refreshToken) {
                refreshToken = newToken;
                saveTokens();
            }
        }
        
        saveTokens();
        Serial.println("✓ Access token refreshed");
        http.end();
        return true;
        
    } else {
        Serial.print("✗ Token refresh failed: ");
        Serial.println(httpCode);
        http.end();
        return false;
    }
}

void getCurrentlyPlaying() {
    if (accessToken.length() == 0) {
        refreshAccessToken();
        if (accessToken.length() == 0) return;
    }
    
    HTTPClient http;
    http.begin("https://api.spotify.com/v1/me/player/currently-playing");
    http.addHeader("Authorization", "Bearer " + accessToken);
    
    int httpCode = http.GET();
    
    if (httpCode == 401) {
        Serial.println("Token expired, refreshing...");
        http.end();
        if (refreshAccessToken()) {
            getCurrentlyPlaying();  // Retry
        }
        return;
    }
    
    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(8192);
        deserializeJson(doc, payload);
        
        // Extract track information
        String trackName = doc["item"]["name"].as<String>();
        String artistName = doc["item"]["artists"][0]["name"].as<String>();
        String albumName = doc["item"]["album"]["name"].as<String>();
        int progressMs = doc["progress_ms"] | 0;
        int durationMs = doc["item"]["duration_ms"] | 0;
        bool playing = doc["is_playing"] | false;
        
        // Create a unique ID for this track
        String trackId = doc["item"]["id"].as<String>();
        
        // Check if track changed
        if (trackId != lastTrackId) {
            lastTrackId = trackId;
            
            // Reset for new track
            currentTrackName = trackName;
            currentArtistName = artistName;
            currentAlbumName = albumName;
            currentProgressMs = progressMs;
            currentDurationMs = durationMs;
            isPlaying = playing;
            
            // Update full display
            updateNowPlayingDisplay(currentTrackName, currentArtistName, 
                                   currentAlbumName, currentProgressMs, 
                                   currentDurationMs, isPlaying);
            
            Serial.println("New track: " + trackName + " - " + artistName);
        } else {
            // Same track, just update progress and play status
            currentProgressMs = progressMs;
            isPlaying = playing;
            
            // Update progress bar
            updateProgressDisplay();
        }
        
    } else if (httpCode == 204) {
        // Nothing playing
        if (isPlaying != false) {
            isPlaying = false;
            drawPlayPauseIcon(false);
            Serial.println("Playback stopped");
        }
    }
    
    http.end();
}