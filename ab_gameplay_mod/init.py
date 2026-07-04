import os
import re
import json
import threading
import tkinter as tk
from tkinter import filedialog, scrolledtext, messagebox, ttk, simpledialog
import concurrent.futures
import logging
import platform
import subprocess

# ==========================================
# CONSTANTES Y CONFIGURACIÓN
# ==========================================
CONFIG_FILE = "file_merger_configs.json"
DEFAULT_CONFIG = {
    "Default": {
        "extensiones": ".txt, .log, .js, .css, .html, .sass, .cs, .aspx, .ts,",
        "tamano": "1",
        "omisiones": "node_modules, .git, .vs, bin, obj,"
    }
}

# ==========================================
# COMPONENTE PERSONALIZADO: UndoEntry
# ==========================================
class UndoEntry(tk.Entry):
    """
    Un widget Entry mejorado con capacidad de Deshacer (Ctrl+Z) y Rehacer (Ctrl+Y).
    Implementa una pila (stack) de historial de cambios.
    """
    def __init__(self, parent, *args, **kwargs):
        super().__init__(parent, *args, **kwargs)
        
        self.stack = [""]  # Pila de historial
        self.index = 0     # Puntero actual en la pila
        self.ignorar_evento = False # Bandera para evitar loops recursivos

        # Bindings (Atajos de teclado)
        self.bind('<Control-z>', self.undo)
        self.bind('<Control-y>', self.redo)
        self.bind('<KeyRelease>', self.record_change)
        
        # Inicializar con el contenido actual si lo hay
        self.stack[0] = self.get()

    def record_change(self, event):
        """Guarda el estado actual en la pila si hubo cambios."""
        # Ignoramos teclas de control que no cambian texto (como Shift, Ctrl, Alt solo)
        if event.keysym in ('Control_L', 'Control_R', 'Shift_L', 'Shift_R', 'Alt_L', 'Alt_R'):
            return
        
        # Si la bandera está activa (ej: durante un undo), no grabamos
        if self.ignorar_evento:
            self.ignorar_evento = False
            return

        current_text = self.get()
        
        # Solo guardar si es diferente al último estado registrado
        if current_text != self.stack[self.index]:
            # Si estamos a mitad de la pila y escribimos, borramos el futuro (nueva rama)
            self.stack = self.stack[:self.index+1]
            self.stack.append(current_text)
            self.index += 1

    def undo(self, event):
        """Retrocede un paso en el historial."""
        if self.index > 0:
            self.index -= 1
            texto_anterior = self.stack[self.index]
            
            self.ignorar_evento = True # Evitar que el cambio del undo se registre como nuevo cambio
            self.delete(0, tk.END)
            self.insert(0, texto_anterior)
        return "break" # Evita comportamiento por defecto si lo hubiera

    def redo(self, event):
        """Avanza un paso en el historial."""
        if self.index < len(self.stack) - 1:
            self.index += 1
            texto_siguiente = self.stack[self.index]
            
            self.ignorar_evento = True
            self.delete(0, tk.END)
            self.insert(0, texto_siguiente)
        return "break"

    # Sobrescribimos insert y delete para mantener el stack sincronizado si se cambia por código
    def insert(self, index, string):
        super().insert(index, string)
        if not self.ignorar_evento:
            self.record_change_manual()

    def delete(self, first, last=tk.END):
        super().delete(first, last)
        if not self.ignorar_evento:
            self.record_change_manual()
            
    def record_change_manual(self):
        """Helper para registrar cambios hechos programáticamente"""
        current_text = self.get()
        if self.index < len(self.stack) and current_text != self.stack[self.index]:
             self.stack = self.stack[:self.index+1]
             self.stack.append(current_text)
             self.index += 1
        elif len(self.stack) == 0:
            self.stack.append(current_text)

# ==========================================
# LÓGICA DE NEGOCIO (Procesamiento)
# ==========================================

def minify_text(text):
    return re.sub(r'\s+', ' ', text).strip()

def leer_archivo(ruta_archivo):
    try:
        with open(ruta_archivo, 'r', encoding='utf-8') as f:
            contenido = f.read()
        return ruta_archivo, contenido
    except Exception as e:
        logging.error(f"Error al leer {ruta_archivo}: {e}")
        return ruta_archivo, None

def process_files(directorio_raiz, extensiones, tamano_maximo_bytes, omisiones, ruta_salida, log_callback, minificar):
    archivos_a_procesar = []
    log_callback("Iniciando recorrido de directorios...")
    
    omisiones_limpias = [o.strip().lower() for o in omisiones if o.strip()]

    for root, dirs, files in os.walk(directorio_raiz):
        dirs[:] = [d for d in dirs if not any(om in d.lower() for om in omisiones_limpias)]

        for file in files:
            ruta_completa = os.path.join(root, file)
            if any(om in ruta_completa.lower() for om in omisiones_limpias): continue
            if not any(ruta_completa.lower().endswith(ext.lower().strip()) for ext in extensiones): continue
            
            try:
                tamano = os.path.getsize(ruta_completa)
                if tamano > tamano_maximo_bytes: continue
            except Exception as e:
                log_callback(f"Error obteniendo tamaño de {ruta_completa}: {e}")
                continue

            archivos_a_procesar.append(ruta_completa)
    
    log_callback(f"Archivos encontrados: {len(archivos_a_procesar)}")
    
    resultados = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        future_to_archivo = {executor.submit(leer_archivo, archivo): archivo for archivo in archivos_a_procesar}
        for future in concurrent.futures.as_completed(future_to_archivo):
            ruta, contenido = future.result()
            if contenido is not None:
                resultados.append((ruta, contenido))
    
    resultados.sort(key=lambda x: x[0])
    
    try:
        with open(ruta_salida, 'w', encoding='utf-8') as salida:
            salida.write("Este archivo contiene la concatenación de varios archivos.\n")
            salida.write("Generado por Python File Merger.\n\n")
            
            for ruta, contenido in resultados:
                if minificar:
                    contenido = minify_text(contenido)
                
                header = f"<<<FILE:{ruta}>>>\n"
                footer = f"<<<END:{ruta}>>>\n"
                salida.write(header)
                salida.write(contenido)
                salida.write("\n" + footer + "\n\n")
                
        log_callback(f"¡Éxito! Archivo generado: {ruta_salida}")

        folder = os.path.dirname(os.path.abspath(ruta_salida))
        if os.name == 'nt':
            os.startfile(folder)
        elif os.name == 'posix':
            subprocess.call(["open" if platform.system() == "Darwin" else "xdg-open", folder])

    except Exception as e:
        log_callback(f"Error crítico al escribir salida: {e}")

# ==========================================
# GESTIÓN DE CONFIGURACIÓN (JSON)
# ==========================================

def load_configs_from_json():
    if not os.path.exists(CONFIG_FILE):
        return DEFAULT_CONFIG.copy()
    try:
        with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
            return json.load(f)
    except json.JSONDecodeError:
        return DEFAULT_CONFIG.copy()

def save_configs_to_json(configs):
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            json.dump(configs, f, indent=4)
    except Exception as e:
        messagebox.showerror("Error", f"No se pudo guardar la configuración: {e}")

# ==========================================
# CONTROLADORES DE INTERFAZ (Handlers)
# ==========================================

def iniciar_proceso():
    directorio = entry_directorio.get()
    if not os.path.isdir(directorio):
        messagebox.showerror("Error", "El directorio raíz no es válido.")
        return

    extensiones = entry_extensiones.get().split(',')
    try:
        max_mb = float(entry_tamano.get())
    except ValueError:
        messagebox.showerror("Error", "El tamaño máximo debe ser un número.")
        return
    
    tamano_bytes = int(max_mb * 1024 * 1024)
    omisiones = entry_omisiones.get().split(',')
    salida = entry_salida.get().strip()
    
    if not salida:
        messagebox.showerror("Error", "Especifique un archivo de salida.")
        return

    minificar = var_minificar.get()
    txt_log.delete(1.0, tk.END)
    log_msg("Iniciando proceso...")

    thread = threading.Thread(
        target=process_files,
        args=(directorio, extensiones, tamano_bytes, omisiones, salida, log_msg, minificar)
    )
    thread.start()

def seleccionar_directorio():
    d = filedialog.askdirectory()
    if d:
        entry_directorio.delete(0, tk.END)
        entry_directorio.insert(0, d)

def seleccionar_salida():
    f = filedialog.asksaveasfilename(defaultextension=".txt", filetypes=[("Text", "*.txt"), ("All", "*.*")])
    if f:
        entry_salida.delete(0, tk.END)
        entry_salida.insert(0, f)

def log_msg(mensaje):
    txt_log.insert(tk.END, mensaje + "\n")
    txt_log.see(tk.END)

def aplicar_configuracion(event=None):
    nombre_config = combo_config.get()
    configs = load_configs_from_json()
    
    if nombre_config in configs:
        data = configs[nombre_config]
        
        entry_extensiones.delete(0, tk.END)
        entry_extensiones.insert(0, data.get("extensiones", ""))
        
        entry_tamano.delete(0, tk.END)
        entry_tamano.insert(0, data.get("tamano", "1"))
        
        entry_omisiones.delete(0, tk.END)
        entry_omisiones.insert(0, data.get("omisiones", ""))
        
        log_msg(f"Configuración '{nombre_config}' cargada.")

def guardar_nueva_configuracion():
    nombre = simpledialog.askstring("Guardar Configuración", "Nombre de la nueva configuración:")
    if not nombre: return

    nueva_data = {
        "extensiones": entry_extensiones.get(),
        "tamano": entry_tamano.get(),
        "omisiones": entry_omisiones.get()
    }
    
    configs = load_configs_from_json()
    configs[nombre] = nueva_data
    save_configs_to_json(configs)
    
    combo_config['values'] = list(configs.keys())
    combo_config.set(nombre)
    log_msg(f"Configuración '{nombre}' guardada.")

def eliminar_configuracion():
    nombre = combo_config.get()
    if not nombre: return

    confirm = messagebox.askyesno("Confirmar", f"¿Eliminar la plantilla '{nombre}'?")
    if confirm:
        configs = load_configs_from_json()
        if nombre in configs:
            del configs[nombre]
            save_configs_to_json(configs)
            
            claves = list(configs.keys())
            combo_config['values'] = claves
            if claves:
                combo_config.set(claves[0])
                aplicar_configuracion()
            else:
                combo_config.set('')
            
            log_msg(f"Configuración '{nombre}' eliminada.")

# ==========================================
# GUI SETUP
# ==========================================
ventana = tk.Tk()
ventana.title("File Merger Pro - Python")
ventana.geometry("650x600")

# --- FRAME DE GESTIÓN DE PLANTILLAS ---
frame_config = tk.LabelFrame(ventana, text="Gestión de Plantillas", padx=10, pady=10)
frame_config.pack(fill="x", padx=10, pady=5)

lbl_combo = tk.Label(frame_config, text="Cargar Config:")
lbl_combo.pack(side="left")

combo_config = ttk.Combobox(frame_config, state="readonly", width=30)
combo_config.pack(side="left", padx=5)
combo_config.bind("<<ComboboxSelected>>", aplicar_configuracion)

btn_guardar_cfg = tk.Button(frame_config, text="💾 Guardar Actual", command=guardar_nueva_configuracion)
btn_guardar_cfg.pack(side="left", padx=5)

btn_borrar_cfg = tk.Button(frame_config, text="🗑 Eliminar", command=eliminar_configuracion, fg="red")
btn_borrar_cfg.pack(side="left", padx=5)

# --- FRAME PRINCIPAL DE DATOS ---
frame_main = tk.Frame(ventana)
frame_main.pack(fill="both", expand=True, padx=10)

# Grid Layout - USANDO NUESTRA CLASE UndoEntry
# Directorio
tk.Label(frame_main, text="Directorio Raíz:").grid(row=0, column=0, sticky="e", pady=5)
entry_directorio = UndoEntry(frame_main, width=50) # <--- CAMBIO AQUÍ
entry_directorio.grid(row=0, column=1, pady=5)
tk.Button(frame_main, text="...", command=seleccionar_directorio).grid(row=0, column=2, padx=5)

# Extensiones
tk.Label(frame_main, text="Extensiones:").grid(row=1, column=0, sticky="e", pady=5)
entry_extensiones = UndoEntry(frame_main, width=50) # <--- CAMBIO AQUÍ
entry_extensiones.grid(row=1, column=1, pady=5)

# Tamaño
tk.Label(frame_main, text="Max Tamaño (MB):").grid(row=2, column=0, sticky="e", pady=5)
entry_tamano = UndoEntry(frame_main, width=50) # <--- CAMBIO AQUÍ
entry_tamano.grid(row=2, column=1, pady=5)

# Omisiones
tk.Label(frame_main, text="Omisiones:").grid(row=3, column=0, sticky="e", pady=5)
entry_omisiones = UndoEntry(frame_main, width=50) # <--- CAMBIO AQUÍ
entry_omisiones.grid(row=3, column=1, pady=5)

# Salida
tk.Label(frame_main, text="Archivo Salida:").grid(row=4, column=0, sticky="e", pady=5)
entry_salida = UndoEntry(frame_main, width=50) # <--- CAMBIO AQUÍ
entry_salida.insert(0, "resultado_final.txt")
entry_salida.grid(row=4, column=1, pady=5)
tk.Button(frame_main, text="Guardar como...", command=seleccionar_salida).grid(row=4, column=2, padx=5)

# Opciones extra
var_minificar = tk.BooleanVar(value=False)
tk.Checkbutton(frame_main, text="Minificar (Compactar código)", variable=var_minificar).grid(row=5, column=1, sticky="w")

# Botón Acción
btn_iniciar = tk.Button(frame_main, text="EJECUTAR FUSIÓN", command=iniciar_proceso, bg="#4CAF50", fg="white", font=("Arial", 10, "bold"))
btn_iniciar.grid(row=6, column=0, columnspan=3, pady=15, sticky="we")

# Log
txt_log = scrolledtext.ScrolledText(frame_main, height=10)
txt_log.grid(row=7, column=0, columnspan=3, sticky="we")

# --- INICIALIZACIÓN ---
configs_iniciales = load_configs_from_json()
combo_config['values'] = list(configs_iniciales.keys())
if "Default" in configs_iniciales:
    combo_config.set("Default")
    aplicar_configuracion()

logging.basicConfig(level=logging.ERROR)
ventana.mainloop()