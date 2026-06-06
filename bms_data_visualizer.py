import pandas as pd
import matplotlib.pyplot as plt

def plot_bms_results(csv_filename="BYS_Analiz_Raporu.csv"):
    try:
        df = pd.read_csv(csv_filename)
    except FileNotFoundError:
        print(f"[HATA] {csv_filename} bulunamadı. Önce C kodunu çalıştırın.")
        return

    fig, ax = plt.subplots(figsize=(12, 7))

    ax.plot(df['Zaman'], df['IdealSoC'], color='blue', linewidth=2.5, label='İdeal SoC (Gerçek Değer)')
    ax.plot(df['Zaman'], df['CoulombCountingSoC'], color='red', linestyle='--', linewidth=2, label='Açık Çevrim (Sadece Akım İntegrali)')
    ax.plot(df['Zaman'], df['PI_Gozlemci_SoC'], color='green', linewidth=2.5, label='PI Gözlemci (Kapalı Çevrim)')

    half_time = df['Zaman'].max() / 2
    ax.axvline(x=half_time, color='black', linestyle=':', linewidth=2, label='Sensör Kayması (Drift) Başlangıcı')

    ax.set_title("Batarya Yönetim Sistemi: Kapalı Çevrim SoC Kestirimi ve Gürbüzlük Testi", fontsize=16, fontweight='bold', pad=15)
    ax.set_xlabel("Zaman (saniye)", fontsize=12)
    ax.set_ylabel("Şarj Durumu - SoC (%)", fontsize=12)
    ax.grid(True, linestyle='--', alpha=0.7)
    
    ax.legend(loc='upper right', fontsize=11, framealpha=0.9)
    
    ax.set_ylim(min(df['PI_Gozlemci_SoC'].min(), df['IdealSoC'].min()) - 5, 105)

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    print("Grafik oluşturuluyor...")
    plot_bms_results()


