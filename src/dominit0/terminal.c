#include <facetos/dominit0/environment.h>
#include <facetos/dominit0/klog.h>
#include <facetos/dominit0/platform/api.h>
#include <facetos/dominit0/terminal.h>
#include <facetos/initrd.h>
#include <facetos/interfaces/IByteReader.h>
#include <facetos/interfaces/IByteWriter.h>
#include <facetos/interfaces/ISeat.h>
#include <facetos/interfaces/ITerminal.h>
#include <facetos/interfaces/ITerminalControl.h>
#include <stdlib.h>
#include <string.h>

typedef struct TerminalAssignmentBinding {
    CurrentDomain *domain;
    size_t assignment_index;
    FacetHandle input, output, control, terminal;
} TerminalAssignmentBinding;

static TerminalAssignmentBinding *assignment_bindings;
static size_t assignment_binding_count;
static Dominit0SystemConfig *terminal_system;

static int add_assignment(CurrentDomain *domain, size_t assignment_index,
                          const CurrentSeatTerminal *terminal)
{
    TerminalAssignmentBinding *expanded = realloc(
        assignment_bindings,
        (assignment_binding_count + 1) * sizeof(*assignment_bindings));
    if (expanded == NULL) return -1;
    assignment_bindings = expanded;
    assignment_bindings[assignment_binding_count++] =
        (TerminalAssignmentBinding){domain, assignment_index,
            terminal->input, terminal->output, terminal->control,
            terminal->terminal};
    return 0;
}

static int discover_terminal(CurrentSeat *seat, size_t index, ISeat *proxy)
{
    const char *configured_name = seat->config->terminals[index];
    FacetString name = {configured_name, strlen(configured_name)};
    CurrentSeatTerminal *current = &seat->terminals[index];
    if (proxy->get_terminal(proxy->self, &name, &current->terminal) != FACET_OK)
        return -1;
    ITerminal *terminal = libfacet_proxy_from_handle(
        &ITerminal_MetaData, current->terminal);
    if (terminal == NULL) return -1;
    FacetResult input = terminal->get_input(terminal->self, &current->input);
    FacetResult output = terminal->get_output(terminal->self, &current->output);
    FacetResult control = terminal->get_control(terminal->self,
                                                &current->control);
    libfacet_free_proxy_client(terminal);
    if (input != FACET_OK || output != FACET_OK || control != FACET_OK)
        return -1;
    current->usable = true;
    return 0;
}

static int start_seats(Dominit0SystemConfig *system)
{
    /* Host tests and future platform discovery may pre-populate the portable
     * runtime array before assignment binding. */
    if (system->current_seats != NULL) return 0;
    PlatformConfigSource source;
    if (platform_get_boot_module(system->parsed.seat_initrd, &source) !=
        PLATFORM_CONFIG_SOURCE_FOUND) {
        klog(LOG_ERROR, "Unable to locate unique seat initrd %s\n",
             system->parsed.seat_initrd);
        return 0;
    }
    FacetInitrd *initrd = facet_initrd_create(source.data, source.size);
    if (initrd == NULL) return 0;
    system->current_seats = calloc(system->parsed.seat_count,
                                   sizeof(*system->current_seats));
    if (system->current_seats == NULL) {
        facet_initrd_destroy(initrd);
        return -1;
    }
    for (size_t i = 0; i < system->parsed.seat_count; i++) {
        CurrentSeat *current = &system->current_seats[i];
        current->config = &system->parsed.seats[i];
        current->terminals = calloc(current->config->terminal_count,
                                    sizeof(*current->terminals));
        if (current->terminals == NULL) continue;
        const uint8_t *elf;
        size_t elf_size;
        if (facet_initrd_find_file(initrd, current->config->server,
                                   &elf, &elf_size) != FACET_OK) {
            klog(LOG_ERROR, "Seat %s server %s is missing from %s\n",
                 current->config->name, current->config->server,
                 system->parsed.seat_initrd);
            continue;
        }
        current->platform_state = platform_start_seat(
            current, elf, elf_size, &current->seat);
        if (current->platform_state == NULL) {
            klog(LOG_ERROR, "Unable to start seat %s from %s\n",
                 current->config->name, current->config->server);
            continue;
        }
        ISeat *proxy = libfacet_proxy_from_handle(&ISeat_MetaData,
                                                  current->seat);
        if (proxy == NULL) continue;
        size_t usable = 0;
        for (size_t j = 0; j < current->config->terminal_count; j++) {
            if (discover_terminal(current, j, proxy) == 0) {
                usable++;
                klog(LOG_INFO, "Discovered terminal %s.%s\n",
                     current->config->name, current->config->terminals[j]);
            } else {
                klog(LOG_ERROR, "Seat %s did not provide terminal %s\n",
                     current->config->name, current->config->terminals[j]);
            }
        }
        libfacet_free_proxy_client(proxy);
        current->usable = usable != 0;
    }
    facet_initrd_destroy(initrd);
    return 0;
}

int dominit0_terminal_initialize(Dominit0SystemConfig *system)
{
    if (system == NULL || system->current_domains == NULL) return -1;
    terminal_system = system;
    if (start_seats(system) != 0) return -1;
    size_t usable_assignments = 0;
    for (size_t di = 0; di < system->domain_count; di++) {
        const FacetConfigDomain *domain = &system->parsed.domains[di];
        for (size_t ai = 0; ai < domain->terminal_count; ai++) {
            const FacetConfigTerminalAssignment *assignment =
                &domain->terminals[ai];
            if (assignment->seat_index >= system->parsed.seat_count) continue;
            CurrentSeat *seat = &system->current_seats[assignment->seat_index];
            if (assignment->terminal_index >= seat->config->terminal_count)
                continue;
            CurrentSeatTerminal *terminal =
                &seat->terminals[assignment->terminal_index];
            if (!terminal->usable) {
                klog(LOG_ERROR, "Terminal %s is unavailable for domain %llu\n",
                     assignment->reference, (unsigned long long)domain->id);
                continue;
            }
            if (add_assignment(system->current_domains[di], ai, terminal) != 0)
                return -1;
            usable_assignments++;
            klog(LOG_INFO, "Assigned terminal %s to domain %llu\n",
                 assignment->reference, (unsigned long long)domain->id);
        }
    }
    if (usable_assignments == 0) {
        klog(LOG_ERROR, "No configured domain has a usable terminal\n");
        return -1;
    }
    return 0;
}

int dominit0_terminal_bind_process_environment(
    CurrentDomain *domain, size_t assignment_index,
    Dominit0ProcessEnvironment *environment)
{
    if (domain == NULL || environment == NULL) return -1;
    for (size_t i = 0; i < assignment_binding_count; i++) {
        TerminalAssignmentBinding *binding = &assignment_bindings[i];
        if (binding->domain != domain ||
            binding->assignment_index != assignment_index)
            continue;
        int result = dominit0_process_environment_bind_terminal(
            environment, binding->input, binding->output, binding->control,
            binding->terminal);
        if (result == 0) {
            dominit0_process_environment_set_terminal_index(
                environment, assignment_index);
            result = dominit0_process_environment_set_terminal_name(
                environment,
                domain->parsed->terminals[assignment_index].reference);
        }
        return result;
    }
    return -1;
}

void dominit0_terminal_destroy(void)
{
    free(assignment_bindings);
    assignment_bindings = NULL;
    assignment_binding_count = 0;
    if (terminal_system != NULL && terminal_system->current_seats != NULL) {
        for (size_t i = 0; i < terminal_system->parsed.seat_count; i++) {
            CurrentSeat *seat = &terminal_system->current_seats[i];
            free(seat->terminals);
            if (seat->seat.platform != NULL)
                (void)libfacet_handle_release(seat->seat);
        }
        free(terminal_system->current_seats);
        terminal_system->current_seats = NULL;
    }
    terminal_system = NULL;
}
