// by busragulen

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

// embedded macros
#define ABS_VAL(x) ((x) < 0 ? -(x) : (x))

// constants and parameters
#define A 2.0                 
#define PEUKERT_K 1.15        
#define R_INTERNAL 0.025      
#define T_AMBIENT 25.0        
#define THERMAL_MASS 37.35    
#define COOLING_COEFF 0.15    

// thevenin
#define R1_POLARIZATION 0.015   
#define C1_POLARIZATION 1200.0 
#define OCV_TEMP_COEFF 0.0005   // OCV temp. constant

// soa limits
#define VOLTAGE_CUTOFF 2.5    
#define TEMP_CUTOFF 60.0      

// control theory
#define OBSERVER_KP 0.002    
#define OBSERVER_KI 0.0005  
#define VOLTAGE_LPF_ALPHA 0.05 
#define MAX_INTEGRAL 25.0       // Anti-windup limit

const double OCV_SOC_REF[11] = {0.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0};
const double OCV_VOLT_REF[11] = {3.00, 3.45, 3.65, 3.70, 3.75, 3.79, 3.85, 3.92, 3.98, 4.08, 4.20};
const double PEUKERT_I_REF[6] = {0.1, 1.0, 5.0, 10.0, 20.0, 40.0};
const double PEUKERT_FAC_REF[6] = {0.70, 1.00, 1.27, 1.41, 1.56, 1.74};

double get_valid_input(const char* prompt) {
    double value;
    int status;
    do {
        printf("%s", prompt);
        status = scanf("%lf", &value);
        while(getchar() != '\n'); 
        if(status != 1 || value <= 0) printf("[HATA] Gecersiz veya negatif deger!\n");
    } while(status != 1 || value <= 0);
    return value;
}

double generate_noise(double max_noise) {
    return ((double)rand() / RAND_MAX) * 2.0 * max_noise - max_noise;
}

double interpolate(double x, const double* x_array, const double* y_array, int size) {
    if (x <= x_array[0]) return y_array[0];
    if (x >= x_array[size - 1]) return y_array[size - 1];
    for (int i = 0; i < size - 1; i++) {
        if (x >= x_array[i] && x <= x_array[i + 1]) {
            double slope = (y_array[i + 1] - y_array[i]) / (x_array[i + 1] - x_array[i]);
            return y_array[i] + slope * (x - x_array[i]);
        }
    }
    return y_array[0];
}

double calculate_ideal_current(uint32_t t) {
    return 1.0 + A * sin(0.1 * (double)t) + (A * 2.0) * cos(0.02 * (double)t);
}

double calculate_dynamic_resistance(double temp, double soc) {
    double r_base = R_INTERNAL; 
    double temp_penalty = (temp < 25.0) ? 1.0 + 0.04 * (25.0 - temp) : 1.0;
    double soc_penalty = (soc < 20.0) ? 1.0 + 0.05 * (20.0 - soc) : 1.0;
    return r_base * temp_penalty * soc_penalty;
}

// Nernst - OCV calculations
double calculate_thermal_ocv(double soc, double temp) {
    double ocv_ref = interpolate(soc, OCV_SOC_REF, OCV_VOLT_REF, 11);
    return ocv_ref + OCV_TEMP_COEFF * (temp - T_AMBIENT);
}

void update_thermal_model(double *temp, double current, double r_dyn, double dt) {
    double ohmic_heat = current * current * r_dyn;
    double cooling = COOLING_COEFF * (*temp - T_AMBIENT);
    *temp += (ohmic_heat - cooling) * dt / THERMAL_MASS;
}

int main() {
    printf(" BYS: 1-RC Model ve PI Gozlemci Tabanli Kapali Cevrim SoC Kestirimi\n");

    double c_nominal = get_valid_input("Batarya Nominal Kapasitesi (Ah): ");
    double soc_initial = get_valid_input("Gercek Baslangic SoC Degeri (1-100 %%): ");
    uint32_t sim_time = (uint32_t)get_valid_input("Simulasyon Suresi (Saniye): ");
    double max_noise_offset = get_valid_input("Maksimum Akim Gurultusu (Orn: 0.05 A): ");

    double dt = 1.0; 
    double soc_ideal = soc_initial;
    double soc_coulomb_counting = soc_initial; 
    double soc_closed_loop = soc_initial - 15.0; // start error
    if (soc_closed_loop < 0) soc_closed_loop = 5.0; 
    
    double cell_temp = T_AMBIENT;
    double error_integral = 0.0;
    int is_contactor_closed = 1; 

    // state variables)
    double vp_ideal = 0.0;
    double vp_est = 0.0;

    double initial_real_ocv = calculate_thermal_ocv(soc_initial, cell_temp);
    double initial_i = calculate_ideal_current(0);
    double initial_r = calculate_dynamic_resistance(cell_temp, soc_initial);
    double v_filtered = initial_real_ocv - (initial_i * initial_r);

    FILE *fp = fopen("BYS_Analiz_Raporu.csv", "w");
    if (fp == NULL) return 1;
    fprintf(fp, "Zaman,Akim,Sicaklik,V_Filtreli,IdealSoC,CoulombCountingSoC,PI_Gozlemci_SoC\n");
    srand(time(NULL));

    printf("\n[SISTEM] Simulasyon basliyor...\n");

    for (uint32_t t = 0; t <= sim_time; t++) {
        double i_ideal = calculate_ideal_current(t);
        if (!is_contactor_closed) i_ideal = 0.0; 

        double i_measured = i_ideal + (max_noise_offset * 0.8) + generate_noise(max_noise_offset);
        if (!is_contactor_closed) i_measured = 0.0; 

        // drift test
        if (t >= sim_time / 2) {
            i_measured += 0.5; 
        }

        double r_dyn_ideal = calculate_dynamic_resistance(cell_temp, soc_ideal);
        update_thermal_model(&cell_temp, i_ideal, r_dyn_ideal, dt);
        
        double dyn_eff = 1.0 - (0.0008 * ABS_VAL(cell_temp - T_AMBIENT));
        if (dyn_eff > 1.0) dyn_eff = 1.0; else if (dyn_eff < 0.8) dyn_eff = 0.8;

        double delta_ideal = 0.0;
        double delta_measured = 0.0;
        double charge_acc = (soc_closed_loop > 90.0) ? 0.90 : 0.99;

        if (i_measured > 0) { // Desarj
            double c_eff_meas_as = (c_nominal / interpolate(i_measured, PEUKERT_I_REF, PEUKERT_FAC_REF, 6)) * 3600.0;
            delta_measured = (i_measured * dt / (c_eff_meas_as * dyn_eff)) * 100.0;
        } else { // Sarj
            double c_eff_meas_as = c_nominal * 3600.0; 
            delta_measured = (i_measured * dt * dyn_eff * charge_acc / c_eff_meas_as) * 100.0;
        }

        if (i_ideal > 0) { // Desarj
            double c_eff_ideal_as = (c_nominal / interpolate(i_ideal, PEUKERT_I_REF, PEUKERT_FAC_REF, 6)) * 3600.0;
            delta_ideal = (i_ideal * dt / (c_eff_ideal_as * dyn_eff)) * 100.0;
        } else { // Sarj
            double c_eff_ideal_as = c_nominal * 3600.0;
            delta_ideal = (i_ideal * dt * dyn_eff * charge_acc / c_eff_ideal_as) * 100.0;
        }

        soc_ideal -= delta_ideal;
        soc_coulomb_counting -= delta_measured;
        soc_closed_loop -= delta_measured; 
        
        //dynamic thevenin 
        double exp_factor = exp(-dt / (R1_POLARIZATION * C1_POLARIZATION));
        vp_ideal = vp_ideal * exp_factor + i_ideal * R1_POLARIZATION * (1.0 - exp_factor);
        vp_est = vp_est * exp_factor + i_measured * R1_POLARIZATION * (1.0 - exp_factor);

        //plant voltage
        double ocv_ideal = calculate_thermal_ocv(soc_ideal, cell_temp);
        
        // Vp
        double v_terminal_ideal = ocv_ideal - vp_ideal - (i_ideal * r_dyn_ideal); 

        double v_raw_adc = v_terminal_ideal + generate_noise(0.040); 
        v_filtered = (VOLTAGE_LPF_ALPHA * v_raw_adc) + ((1.0 - VOLTAGE_LPF_ALPHA) * v_filtered);

        if (is_contactor_closed && (cell_temp > TEMP_CUTOFF || v_filtered < VOLTAGE_CUTOFF)) {
            printf("[KRITIK UYARI] t=%u s: SOA asildi! Akim kesildi.\n", t);
            is_contactor_closed = 0;
        }
        
        // pl observer
        
        double r_dyn_est = calculate_dynamic_resistance(cell_temp, soc_closed_loop);
        double ocv_estimated = v_filtered + (i_measured * r_dyn_est) + vp_est;
        double ocv_ref_estimated = ocv_estimated - OCV_TEMP_COEFF * (cell_temp - T_AMBIENT);
        double soc_from_ocv = interpolate(ocv_ref_estimated, OCV_VOLT_REF, OCV_SOC_REF, 11);
        double soc_error = soc_from_ocv - soc_closed_loop;
        
        if (!((soc_closed_loop >= 100.0 && soc_error > 0) || (soc_closed_loop <= 0.0 && soc_error < 0))) {
            error_integral += soc_error * dt;
        }

        //clamp
        if (error_integral > MAX_INTEGRAL) error_integral = MAX_INTEGRAL;
        if (error_integral < -MAX_INTEGRAL) error_integral = -MAX_INTEGRAL;

        soc_closed_loop += (OBSERVER_KP * soc_error) + (OBSERVER_KI * error_integral);

        // SoC 
        soc_ideal = (soc_ideal < 0) ? 0 : ((soc_ideal > 100) ? 100 : soc_ideal);
        soc_coulomb_counting = (soc_coulomb_counting < 0) ? 0 : ((soc_coulomb_counting > 100) ? 100 : soc_coulomb_counting);
        soc_closed_loop = (soc_closed_loop < 0) ? 0 : ((soc_closed_loop > 100) ? 100 : soc_closed_loop);

        fprintf(fp, "%u,%.3f,%.2f,%.3f,%.4f,%.4f,%.4f\n", 
                t, i_measured, cell_temp, v_filtered, soc_ideal, soc_coulomb_counting, soc_closed_loop);
    }
    fclose(fp);
    printf("[SISTEM] Simulasyon tamamlandi. BYS_Analiz_Raporu.csv olusturuldu.\n");
    return 0;
}


