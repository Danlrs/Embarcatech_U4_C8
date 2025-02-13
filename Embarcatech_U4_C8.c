#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "pico/bootrom.h"

// Definição de pinos e parâmetros
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define JOYSTICK_X 26 
#define JOYSTICK_Y 27  
#define JOYSTICK_BUTTON 22 
#define A_BUTTON 5 
#define B_BUTTON 6
#define RED_LED 13
#define BLUE_LED 12
#define GREEN_LED 11
#define PWM_FREQ 5000

// Variáveis globais para controle de estado
static volatile uint32_t last_event = 0;
bool pwm_leds = true;
bool frame = false;

// Configuração do PWM para um pino específico
void pwm_setup(uint pin, uint frequency) {
  gpio_set_function(pin, GPIO_FUNC_PWM);
  uint slice_num = pwm_gpio_to_slice_num(pin);
  pwm_set_wrap(slice_num, 255);
  pwm_set_clkdiv(slice_num, (float)48000000 / frequency / 256);
  pwm_set_enabled(slice_num, true);
}

// Função de callback para tratamento de eventos dos botões
void button_pressed(uint gpio, uint32_t events){
  printf("Botão pressionado: GPIO %d\n", gpio);
  uint32_t current_time = to_us_since_boot(get_absolute_time());

  // Debounce para evitar múltiplas leituras em curto intervalo
  if(current_time - last_event > 300000){
      last_event = current_time;

      // Verifica qual botão foi pressionado e executa a ação correspondente
      if(gpio == JOYSTICK_BUTTON){
          gpio_put(GREEN_LED, !gpio_get(GREEN_LED));
          frame = !frame;
      }else if(gpio == B_BUTTON){
        reset_usb_boot(0, 0); // Reinicia no modo USB Boot
      }else if(gpio == A_BUTTON){
        pwm_leds = !pwm_leds; // Alterna o controle de PWM nos LEDs
      }

      // Se o controle PWM estiver desativado, desliga os LEDs
      if(!pwm_leds){
        pwm_set_gpio_level(RED_LED, 0);
        pwm_set_gpio_level(BLUE_LED, 0);
      }
  }
}

// Função para converter leitura do ADC para brilho PWM (0-255)
uint8_t calcular_pwm(uint16_t adc_value) {
  return (uint8_t)((abs(2048 - adc_value) / 2048.0) * 255);
}

int main(){
    // Configuração do PWM para os LEDs
    pwm_setup(RED_LED, PWM_FREQ);
    pwm_setup(BLUE_LED, PWM_FREQ);

    // Configuração do LED verde
    gpio_init(GREEN_LED);
    gpio_set_dir(GREEN_LED, GPIO_OUT);

    // Configuração dos botões e suas interrupções
    gpio_init(B_BUTTON);
    gpio_set_dir(B_BUTTON, GPIO_IN);
    gpio_pull_up(B_BUTTON);
    gpio_set_irq_enabled_with_callback(B_BUTTON, GPIO_IRQ_EDGE_FALL, true, &button_pressed);
    
    gpio_init(JOYSTICK_BUTTON);
    gpio_set_dir(JOYSTICK_BUTTON, GPIO_IN);
    gpio_pull_up(JOYSTICK_BUTTON);
    gpio_set_irq_enabled_with_callback(JOYSTICK_BUTTON, GPIO_IRQ_EDGE_FALL, true, &button_pressed);
    
    gpio_init(A_BUTTON);
    gpio_set_dir(A_BUTTON, GPIO_IN);
    gpio_pull_up(A_BUTTON);
    gpio_set_irq_enabled_with_callback(A_BUTTON, GPIO_IRQ_EDGE_FALL, true, &button_pressed);

    // Inicialização do barramento I2C
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicialização do display OLED
    ssd1306_t ssd;
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // Limpa o display
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    // Inicialização do ADC para leitura do joystick
    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);

    float pos_x = 0;  // Inicializa posição do cursor no display
    float pos_y = 0;
    uint16_t adc_value_x;
    uint16_t adc_value_y;  
    bool cor = true;

    // Loop principal
    while (true){
        // Leitura dos valores do joystick
        adc_select_input(0);
        adc_value_x = adc_read();
        adc_select_input(1);
        adc_value_y = adc_read();

        // Normaliza a leitura para coordenadas do display
        float target_x = 64 - (adc_value_x / 4095.0) * 64;
        float target_y = (adc_value_y / 4095.0) * 128;

        // Ajusta os limites para manter o cursor dentro da tela
        pos_x = target_x;
        pos_y = target_y;
        if(pos_x < 0) pos_x = 0;
        if(pos_x > 56) pos_x = 56;
        if(pos_y < 0) pos_y = 0;
        if(pos_y > 120) pos_y = 120;

        // Atualiza o display
        ssd1306_fill(&ssd, !cor);

        // Ajusta os LEDs conforme a posição do joystick
        if(pwm_leds){
            pwm_set_gpio_level(RED_LED, calcular_pwm(adc_value_y));
            pwm_set_gpio_level(BLUE_LED, calcular_pwm(adc_value_x));
            if(adc_value_x == 2048){
                pwm_set_gpio_level(RED_LED, 0);
            }
            if (adc_value_y == 2048){
                pwm_set_gpio_level(BLUE_LED, 0);
            }
        }
        
        // Atualiza o contorno do display
        if(frame){
            ssd1306_rect(&ssd, 0, 0, 128, 64, cor, !cor);
        }else{
            ssd1306_rect(&ssd, 0, 0, 128, 64, !cor, cor);
        }

        // Desenha o cursor no display
        ssd1306_rect(&ssd, (int)pos_x, (int)pos_y, 8, 8, cor, cor);
        ssd1306_send_data(&ssd);

        sleep_ms(10); // Pequeno delay para suavizar animação
    }
}
