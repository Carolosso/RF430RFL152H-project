package com.example.rfidcommandsender;

import android.app.PendingIntent;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Color;
import android.nfc.NfcAdapter;
import android.nfc.Tag;
import android.nfc.tech.NfcV;
import android.os.Bundle;
import android.os.Handler;
import android.view.View;
import android.widget.*;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.FileProvider;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.components.XAxis;
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.data.LineData;
import com.github.mikephil.charting.data.LineDataSet;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    private NfcAdapter nfcAdapter;
    private Tag currentTag;
    private EditText repeatCountInput;
    private TextView responseView, logView;
    private ScrollView logScroll;
    private final Handler handler = new Handler();
    private boolean infinite = false;
    private int repeat = 0;
    private int counter = 0;
    private byte[] command;
    private double gainFactor = 1.0;

    private long startTimeMillis = 0;
    TextView voltageValue;

    private LineChart voltageChart;
    private LineDataSet dataSet;
    private LineData lineData;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        logView = findViewById(R.id.logView);
        logScroll = findViewById(R.id.logScroll);
        repeatCountInput = findViewById(R.id.repeatCount);
        responseView = findViewById(R.id.responseView);
        voltageValue = findViewById(R.id.voltageValue);
        Button measureButton = findViewById(R.id.measureButton);
        Button clearLogButton = findViewById(R.id.clearLogButton);
        Button exportButton = findViewById(R.id.exportButton);

        exportButton.setOnClickListener(v -> exportDataToCsv());

        clearLogButton.setOnClickListener(v -> {
            logView.setText("");
            dataSet.clear();
            lineData.notifyDataChanged();
            voltageChart.notifyDataSetChanged();
            voltageChart.invalidate();
        });

        voltageChart = findViewById(R.id.voltageChart);
        dataSet = new LineDataSet(new ArrayList<>(), "Voltage (V)");
        dataSet.setLineWidth(2f);
        dataSet.setColor(Color.GREEN);
        dataSet.setDrawCircles(false);
        dataSet.setDrawValues(false);

        lineData = new LineData(dataSet);
        voltageChart.setData(lineData);
        voltageChart.getXAxis().setPosition(XAxis.XAxisPosition.BOTTOM);
        voltageChart.getXAxis().setValueFormatter(new com.github.mikephil.charting.formatter.ValueFormatter() {
            @Override
            public String getFormattedValue(float value) {
                return String.format("%.1f s", value / 1000f);
            }
        });
        voltageChart.getDescription().setText("");
        voltageChart.getXAxis().setTextColor(Color.GRAY);
        voltageChart.getAxisLeft().setTextColor(Color.GRAY);
        voltageChart.getAxisRight().setTextColor(Color.GRAY);
        voltageChart.getLegend().setTextColor(Color.GRAY);

        nfcAdapter = NfcAdapter.getDefaultAdapter(this);

        measureButton.setOnClickListener(v -> {
            String repeatText = repeatCountInput.getText().toString().trim();
            repeat = (repeatText.isEmpty()) ? 1 : Integer.parseInt(repeatText);
            infinite = repeat == 0;
            counter = 0;

            byte[] configCommand = hexStringToByteArray("02A307");
            appendLog("➡ Wysyłanie komendy A3 (odczyt konfiguracji): " + bytesToHex(configCommand));
            try {
                byte[] configResponse = sendCommand(currentTag, configCommand);
                appendLog("⬅ Odpowiedź A3: " + bytesToHex(configResponse));

                if (configResponse.length >= 5 && configResponse[1] == (byte) 0xA3) {
                    int regValue = configResponse[3] & 0xFF;
                    int gainBits = (regValue >> 3) & 0b11;
                    switch (gainBits) {
                        case 0: gainFactor = 1.0; break;
                        case 1: gainFactor = 2.0; break;
                        case 2: gainFactor = 4.0; break;
                        case 3: gainFactor = 8.0; break;
                        default: gainFactor = 1.0; break;
                    }
                    appendLog("✔ Odczytany GAIN z odpowiedzi: " + (int) gainFactor + "x");
                } else {
                    appendLog("❌ Błąd odpowiedzi A3 lub format niepoprawny.");
                    return;
                }
            } catch (IOException e) {
                appendLog("❌ Błąd wysyłania komendy A3: " + e.getMessage());
                return;
            }

            try {
                byte[] a1Command = hexStringToByteArray("02A107");
                appendLog("➡ Wysyłanie komendy A1 (zasilanie ON): " + bytesToHex(a1Command));
                byte[] a1Response = sendCommand(currentTag, a1Command);
                appendLog("⬅ Odpowiedź A1: " + bytesToHex(a1Response));
            } catch (IOException e) {
                appendLog("❌ Błąd wysyłania komendy A1: " + e.getMessage());
                return;
            }

            command = hexStringToByteArray("02A207");
            if (currentTag != null) startRepeatingCommand();
        });
    }

    private void startRepeatingCommand() {
        dataSet.clear();
        appendLog("Rozpoczynanie pomiaru...");

        lineData.notifyDataChanged();
        voltageChart.notifyDataSetChanged();
        voltageChart.invalidate();

        startTimeMillis = System.currentTimeMillis();

        Runnable commandTask = new Runnable() {
            @Override
            public void run() {
                if (infinite || counter < repeat) {
                    try {
                        if (repeat <= 50 || infinite) {
                            appendLog("➡ Wysyłanie komendy A2 (pomiar): " + bytesToHex(command));
                        }

                        byte[] response = sendCommand(currentTag, command);

                        if (repeat <= 50 || infinite) {
                            appendLog("⬅ Odpowiedź A2: " + bytesToHex(response));
                        }

                        double voltage = calculateVoltageFromResponse(response);
                        if (voltage >= 0) {
                            long currentTime = System.currentTimeMillis();
                            float elapsedMs = currentTime - startTimeMillis;
                            voltageValue.setText(String.format("%.4f V", voltage));

                            lineData.addEntry(new Entry(elapsedMs, (float) voltage), 0);
                            lineData.notifyDataChanged();
                            voltageChart.notifyDataSetChanged();
                            voltageChart.setVisibleXRangeMaximum(3000);
                            voltageChart.moveViewToX(elapsedMs);
                        }

                        counter++;
                        handler.post(this);

                        if (!infinite && counter >= repeat) {
                            appendLog("Pomiar zakończony, wyłączanie zasilania...");

                            try {
                                byte[] a4Command = hexStringToByteArray("02A407");
                                appendLog("➡ Wysyłanie komendy A4 (zasilanie OFF): " + bytesToHex(a4Command));
                                byte[] a4Response = sendCommand(currentTag, a4Command);
                                appendLog("⬅ Odpowiedź A4: " + bytesToHex(a4Response));
                                appendLog("✔ Zasilanie układu wyłączone.");
                            } catch (IOException e) {
                                appendLog("Błąd podczas wysyłania A4: " + e.getMessage());
                            }
                        }

                    } catch (IOException e) {
                        appendLog("Błąd pomiaru A2: " + e.getMessage());
                    }
                }
            }
        };

        handler.post(commandTask);
    }

    private byte[] sendCommand(Tag tag, byte[] command) throws IOException {
        NfcV nfcv = NfcV.get(tag);
        nfcv.connect();
        byte[] response = nfcv.transceive(command);
        nfcv.close();
        return response;
    }

    private double calculateVoltageFromResponse(byte[] response) {
        if (response.length < 5 ||
                (response[1] != (byte) 0xA2 && response[1] != (byte) 0xA3)) {
            return -1;
        }
        int highByte = response[4] & 0xFF;
        int lowByte = response[3] & 0xFF;
        int adcValue = (highByte << 8) | lowByte;
        final int ADC_MAX = (1 << 14) - 1;
        final double VREF = 0.9;
        return ((adcValue / (double) ADC_MAX) * VREF) / gainFactor;
    }

    private String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) sb.append(String.format("%02X ", b));
        return sb.toString().trim();
    }

    private byte[] hexStringToByteArray(String s) {
        int len = s.length();
        byte[] data = new byte[len / 2];
        for (int i = 0; i < len; i += 2) {
            data[i / 2] = (byte) ((Character.digit(s.charAt(i), 16) << 4)
                    + Character.digit(s.charAt(i + 1), 16));
        }
        return data;
    }

    private void appendLog(String message) {
        runOnUiThread(() -> {
            logView.append(message + "\n");
            logScroll.post(() -> logScroll.fullScroll(View.FOCUS_DOWN));
        });
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        currentTag = intent.getParcelableExtra(NfcAdapter.EXTRA_TAG);
        Toast.makeText(this, "NFC Tag detected", Toast.LENGTH_SHORT).show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        Intent intent = new Intent(this, getClass()).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_MUTABLE);
        IntentFilter[] filters = new IntentFilter[]{};
        String[][] techList = new String[][]{new String[]{NfcV.class.getName()}};
        nfcAdapter.enableForegroundDispatch(this, pendingIntent, filters, techList);
    }

    @Override
    protected void onPause() {
        super.onPause();
        nfcAdapter.disableForegroundDispatch(this);
    }


    private void exportDataToCsv() {
        if (dataSet.getEntryCount() == 0) {
            Toast.makeText(this, "Brak danych do eksportu", Toast.LENGTH_SHORT).show();
            return;
        }

        StringBuilder csv = new StringBuilder();
        csv.append("Time (ms),Voltage (V)\n");

        for (Entry entry : dataSet.getValues()) {
            csv.append(String.format(Locale.US, "%.0f,%.4f\n", entry.getX(), entry.getY()));
        }

        try {
            File csvFile = new File(getExternalFilesDir(null), "pomiar.csv");
            FileWriter writer = new FileWriter(csvFile);
            writer.write(csv.toString());
            writer.flush();
            writer.close();

            Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType("text/csv");
            intent.putExtra(Intent.EXTRA_STREAM, FileProvider.getUriForFile(
                    this, getPackageName() + ".provider", csvFile));
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(intent, "Udostępnij dane CSV"));

        } catch (IOException e) {
            Toast.makeText(this, "Błąd eksportu: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }
}
